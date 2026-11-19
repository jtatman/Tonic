# Phase 1: New Core Design ("Tonic 2")

Status: **draft for review** (2026-11-18). Tracked in beads epic `Phase 1`. Decisions are recorded in mnemoria.

## 1. Why a new core

Phase 0 made Tonic build cleanly everywhere, but these limits are structural and live in the core
(`TonicCore`, `Generator`, `BufferFiller`, `Synth`, `TonicFrames`):

| Limit in Tonic 1 | Needed for |
|---|---|
| One global sample rate, fixed 64-sample blocks | plugins (each instance has its own rate; hosts send any block size) |
| Non-atomic ref-counted handles shared by UI and audio threads; objects can be freed on the audio thread | crash-free plugins |
| Mutex taken on the audio thread every block | dropout-free realtime audio |
| Mono/stereo only (`isStereoOutput` flag) | buses, mixer, surround, effects routing |
| Parameters change once per block, no event timestamps | sample-accurate MIDI and host automation |
| No voices or voice allocation | polyphony, MPE |
| Mixer is a plain sum | a real mixer library |
| 2013-era DSP (biquads, no oversampling) | sound quality comparable to current synths |

What we keep: the **patching syntax** (`(saw + saw2) * lfo >> LPF24().cutoff(800 + env * 2000)`), the split
between audio-rate and control-rate signals, named parameters, and the Unlicense.

## 2. Goals and non-goals

Goals
- Realtime-safe audio path: no locks, no allocation, no frees, no exceptions inside `process()`.
- No global state: everything lives in an engine instance.
- Variable block size and any sample rate, set per instance.
- Multichannel buses; a mixer with strips, sends, groups and insert chains.
- Sample-accurate events (notes, parameter changes, note expressions).
- Polyphony with voice allocation (poly, mono, legato, unison) and MPE-style per-note expressions.
- A DSP layer simple enough to port line-for-line to Rust and Go.
- The same engine runs as plugin, standalone app, and offline renderer.

Non-goals (for Phase 1)
- Plugin format code inside the library (that is the adapter layer, using DPF).
- GUI. The core exposes parameters and macros; UIs are built on top.
- AI features (they arrive later as clients of the parameter/macro API).

## 3. Layers

```
  DSL front-end      saw(110) * lfo >> lpf24(cutoff)          builds a graph description (non-RT)
        │
  Graph builder      nodes + ports + connections + params     validates, allocates buffers (non-RT)
        │  compile
  Compiled graph     flat schedule, preallocated buffers      swapped into audio thread atomically
        │
  Engine             process(ctx): events, voices, buses      the only code on the audio thread
        │
  DSP kernels        oscillators, filters, envelopes, ...     plain structs, no virtuals, no allocation
```

Adapters sit outside the library: `tonic-dpf` (VST3/CLAP/LV2/AU/JACK via DPF), `tonic-standalone`
(audio device I/O), `tonic-render` (offline WAV rendering for tests and batch work).

### 3.1 DSP kernels (`tonic/dsp/`)

Plain structs with a fixed shape:

```cpp
struct SvfZdf {
  void prepare(double sampleRate, int maxBlock);   // may allocate; never called on the audio thread
  void reset();                                    // clear state
  float process(float in);                         // per sample
  void  process(const float* in, float* out, int n);
  // parameters are plain setters; smoothing is the caller's job
};
```

Rules: no virtual functions, no allocation after `prepare`, no global state, no exceptions,
header-only where reasonable. This layer is what gets ported to Rust and Go, and what the golden
render tests pin down.

Initial kernel set (port + upgrade):

| Kernel | Source |
|---|---|
| minBLEP/BLAMP oscillators (saw, square/PWM, triangle), hard sync | MauSynth rebuild (better than Tonic's BLEP) |
| Wavetable oscillator | Tonic `TableLookupOsc`, add mip-mapping |
| ZDF state-variable filter (LP/BP/HP/notch), ZDF 4-pole ladder with saturation | MauSynth rebuild + new SVF |
| Biquads (shelves, peaking EQ) | Tonic `Filters` |
| Exponential ADSR, multi-segment envelope | MauSynth rebuild / Tonic `ADSR` |
| LFOs, noise (white/pink, xorshift RNG instead of `rand()`) | Tonic `LFNoise`, `Noise` |
| Delay line with interpolation, stereo/ping-pong delay, chorus | Tonic `DelayUtils`, MauSynth chorus |
| Reverb (start: Tonic's, later an FDN) | Tonic `Reverb` |
| Compressor/limiter, bitcrusher | Tonic |
| 2x/4x oversampling (polyphase halfband) | MauSynth rebuild |
| Parameter smoother (one-pole and linear ramp) | new |

### 3.2 Graph builder and compiled graph (`tonic/graph/`)

- A **Node** declares ports: audio inputs/outputs (each a bus with a channel count) and control inputs.
- The builder runs off the audio thread. It checks types and channel counts, topologically sorts
  nodes, assigns buffers (reusing them where lifetimes don't overlap), and produces a
  **CompiledGraph**: a flat array of `{node*, input buffers, output buffers}` steps.
- Hot-swap: the engine holds an atomic pointer to the current CompiledGraph. A new graph is published
  with an atomic exchange; the old one goes onto a lock-free retire queue and is freed by a non-RT
  thread. (Replaces Tonic 1's mutex and per-generator "already computed this block" cache.)
- Fan-out is free: a node runs once per block and every consumer reads its output buffer.

### 3.3 Engine and process context

```cpp
struct ProcessContext {
  double sampleRate;
  int numFrames;                 // any size up to maxBlock
  const EventList& events;       // time-stamped (sample offset) notes, params, expressions
  const Transport* transport;    // tempo, position, playing (optional)
  AudioBuses inputs, outputs;
};
void Engine::process(const ProcessContext& ctx);
```

Sample-accurate events: the engine splits the block at event timestamps (as CLAP hosts expect) and
processes sub-blocks between events. Parameter changes feed smoothers, so audio stays click-free.

### 3.4 Parameters and macros

- Every parameter has a stable `ParamId`, a range, a mapping (linear, log, enum, bipolar), a default,
  and a smoothing time. Plugin automation, MIDI CC learn and presets all address `ParamId`s.
- UI→audio and audio→UI traffic uses lock-free single-producer/single-consumer queues. Meters and
  current values flow back the same way.
- **Macros**: one macro drives many parameters through per-target curves and ranges. This is the
  basis for the minimalist control surface (8 macros on a small MIDI controller; detail parameters
  live in submenus).

### 3.5 Voices

- A **voice** is a sub-graph instantiated N times (preallocated at build time).
- VoiceManager: poly / mono / legato / unison (with detune and spread), stealing policy
  (oldest, quietest, same note), note IDs for per-note expressions (pitch, pressure, timbre, pan),
  matching CLAP note expressions and MPE.
- Voices write into a voice-sum bus; global effects follow.

### 3.6 Buses, mixer and effects routing

- **Bus**: channel layout (mono, stereo, N channels) plus a buffer per channel.
- **Mixer** node: channel strips (gain, pan with selectable pan law, mute/solo, meters), insert
  chains, sends to return buses, group buses.
- **FX matrix** (the "effects multiplexer"): K effect slots with a routing matrix that can express
  serial, parallel and feedback-free mixed routings, with per-cell gain and click-free crossfades
  when the routing changes.

### 3.7 DSL front-end

The Tonic 1 syntax stays, but handles become **cheap value types that describe nodes in a builder**,
not ref-counted live objects:

```cpp
Patch p;
auto cutoff = p.param("cutoff", 200, 8000, 1200, Map::Log);
auto env    = adsr(0.01, 0.2, 0.7, 0.4).trigger(p.gate());
auto osc    = saw(p.pitch()) + saw(p.pitch() * 1.005f);
p.output(osc * env >> ladder24(cutoff + env * 3000).resonance(0.3));
CompiledGraph g = p.compile(sampleRate, maxBlock);   // off the audio thread
```

Operator overloading produces nodes; `>>` connects an output to an effect's main input. Nothing
in the DSL touches the audio thread.

## 4. Threading model

| Thread | Does | Never does |
|---|---|---|
| Audio | `Engine::process`, reads queues, runs the compiled graph | lock, allocate, free, throw, log |
| Control/UI | builds and compiles graphs, pushes parameter changes, reads meters | touch live graph state directly |
| Housekeeping | frees retired graphs and buffers | — |

A debug build installs an allocation hook that fails tests if `process()` allocates.

## 5. Testing

- **Golden renders**: each kernel and example patch renders a fixed input to a reference file;
  tests compare within a tolerance. Ports to Rust/Go must pass the same files.
- **Behavior tests** (carried over from Phase 0): sample-rate correctness, timing, stability.
- **RT-safety tests**: allocation hook and lock detection around `process()`.
- **Benchmarks**: voices per core at 48 kHz for reference patches, tracked in CI.

## 6. Project layout

```
tonic2/
  include/tonic/dsp/      kernels (header-only)
  include/tonic/graph/    builder, compiled graph, nodes
  include/tonic/engine/   engine, events, voices, params, macros, mixer
  include/tonic/dsl/      patch syntax
  src/                    non-header parts
  tests/  bench/  examples/
  adapters/render/        offline WAV renderer (in-repo, used by tests)
```

Separate repos later: `tonic-dpf` (plugins), `tonic-standalone` (device I/O).

## 7. Milestones

| # | Milestone | Done when |
|---|---|---|
| M1 | Skeleton + DSP kernels v1 | repo, CMake, CI; oscillators, SVF, ladder, ADSR, smoother; golden tests pass |
| M2 | Graph builder + compiled graph + engine | variable block sizes, multichannel buses, atomic graph swap; RT-safety test passes |
| M3 | Params, events, macros | sample-accurate events, smoothing, macro mapping; offline renderer |
| M4 | DSL front-end | Tonic 1 demo synths rewritten in the new syntax and rendering |
| M5 | Voices | poly/mono/legato/unison, stealing, note expressions; benchmark recorded |
| M6 | Mixer + FX matrix | strips, sends, groups, routing matrix with crossfades |
| M7 | First adapter | `tonic-dpf` plugin (VST3/CLAP/LV2) of one example synth, tested in a host |

## 8. Decisions needed

| Question | Recommendation |
|---|---|
| Where does it live? | New repo `jtatman/tonic2` (Unlicense); Tonic 1 stays as the maintained classic. |
| C++ standard | C++20 (concepts, `std::span`), still buildable by current MSVC, clang, gcc. |
| Sample type | `float` for audio, `double` for phase/time accumulators. |
| Standalone audio I/O | miniaudio (public domain / MIT-0, single file, covers WASAPI, CoreAudio, ALSA, PulseAudio, JACK). ASIO via RtAudio only if needed, because the ASIO SDK changes licensing. |
| Plugin adapter | DPF (ISC license), as already used in mausynth-rebuild. |
| MauSynth DSP reuse | Port its oscillators, ladder, envelopes and oversampling into `tonic/dsp` (same author, compatible license to confirm). |
