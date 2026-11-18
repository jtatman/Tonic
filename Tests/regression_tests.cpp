// Headless regression tests for Tonic. No audio device or test framework needed.
// Each test renders offline through Synth::fillBufferOfFloats and checks measurable behavior.
// Run via ctest, or directly: ./tonic_tests

#include "Tonic.h"
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

using namespace Tonic;

namespace {

int failures = 0;

void check(bool ok, const std::string &name, const std::string &detail) {
  std::printf("[%s] %s  (%s)\n", ok ? "PASS" : "FAIL", name.c_str(), detail.c_str());
  if (!ok) failures++;
}

std::vector<float> render(Synth &s, int frames, int channels = 1) {
  std::vector<float> buf(static_cast<size_t>(frames) * channels);
  s.fillBufferOfFloats(buf.data(), frames, channels);
  return buf;
}

int zeroCrossings(const std::vector<float> &b) {
  int zc = 0;
  for (size_t i = 1; i < b.size(); i++)
    if ((b[i - 1] < 0) != (b[i] < 0)) zc++;
  return zc;
}

bool allFinite(const std::vector<float> &b) {
  for (float v : b) if (!std::isfinite(v)) return false;
  return true;
}

// Sine frequency must follow the configured sample rate.
void testSineFrequency(float sr) {
  setSampleRate(sr);
  Synth s;
  s.setOutputGen(SineWave().freq(1000));
  auto b = render(s, static_cast<int>(sr));  // one second
  int zc = zeroCrossings(b);
  // ~7 crossings are consistently lost at startup at every rate (oscillator phase/param
  // ramp-in during the first blocks); a sample-rate error would be off by hundreds.
  check(std::abs(zc - 2000) <= 12, "sine 1 kHz @ " + std::to_string((int)sr),
        "zero crossings=" + std::to_string(zc) + ", expect ~2000");
}

// ADSR is implemented in ADSR.cpp (a separate translation unit). Before the fix,
// setSampleRate() did not reach it and envelopes always timed at 44.1 kHz.
void testAdsrTiming(float sr) {
  setSampleRate(sr);
  ControlTrigger t;
  ADSR env = ADSR().attack(1.0).decay(0.01).sustain(1).release(0.01).trigger(t);
  Synth s;
  s.setOutputGen(env);
  t.trigger();
  auto b = render(s, static_cast<int>(sr * 2));
  float half = b[static_cast<size_t>(sr / 2)];
  check(std::abs(half - 0.5f) < 0.02f, "ADSR 1 s attack @ " + std::to_string((int)sr),
        "value at 0.5 s=" + std::to_string(half) + ", expect ~0.5");
}

// BasicDelay buffer length is computed in DelayUtils.cpp (separate TU).
void testDelayTiming(float sr) {
  setSampleRate(sr);
  ControlTrigger t;
  // A single block-length click, delayed by 0.25 s, fully wet.
  Generator click = ADSR().attack(0).decay(0.0005).sustain(0).release(0).trigger(t);
  Synth s;
  s.setOutputGen(click >> BasicDelay(0.5).delayTime(0.25).wetLevel(1).dryLevel(0).feedback(0));
  t.trigger();
  auto b = render(s, static_cast<int>(sr));
  size_t peak = 0;
  for (size_t i = 0; i < b.size(); i++) if (std::abs(b[i]) > std::abs(b[peak])) peak = i;
  double secs = peak / sr;
  check(std::abs(secs - 0.25) < 0.01, "BasicDelay 0.25 s @ " + std::to_string((int)sr),
        "echo peak at " + std::to_string(secs) + " s");
}

// A dense patch must stay finite (no NaN/Inf) and non-silent.
void testDensePatchStable() {
  setSampleRate(48000);
  Generator sum = SawtoothWave().freq(110);
  for (int v = 1; v < 16; v++) sum = sum + SawtoothWave().freq(110 + v * 0.7f);
  Synth s;
  s.setOutputGen((sum * 0.05f) >> LPF24().cutoff(1200).Q(2) >> Reverb());
  auto b = render(s, 48000 * 2, 2);
  double rms = 0;
  for (float v : b) rms += v * v;
  rms = std::sqrt(rms / b.size());
  check(allFinite(b) && rms > 1e-3, "16 saws + LPF24 + reverb stable",
        "rms=" + std::to_string(rms));
}

}  // namespace

int main() {
  for (float sr : {44100.f, 48000.f, 96000.f}) {
    testSineFrequency(sr);
    testAdsrTiming(sr);
    testDelayTiming(sr);
  }
  testDensePatchStable();
  std::printf("\n%d failure(s)\n", failures);
  return failures == 0 ? 0 : 1;
}
