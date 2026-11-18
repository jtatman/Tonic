cmake_minimum_required(VERSION 3.20)
project(Tonic VERSION 0.2.0 LANGUAGES CXX)

option(TONIC_BUILD_TESTS    "Build headless regression tests" ON)
option(TONIC_BUILD_EXAMPLES "Build the RtAudio standalone demo (needs an audio backend)" OFF)
option(TONIC_WARNINGS       "Enable extra compiler warnings" ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

file(GLOB TONIC_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/Tonic/*.cpp)
file(GLOB TONIC_HEADERS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/Tonic/*.h)

add_library(tonic STATIC ${TONIC_SOURCES} ${TONIC_HEADERS} src/Tonic.h)
add_library(Tonic::tonic ALIAS tonic)
target_include_directories(tonic PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/Tonic>)
target_compile_features(tonic PUBLIC cxx_std_17)

if(MSVC)
  target_compile_definitions(tonic PUBLIC _USE_MATH_DEFINES NOMINMAX)
  if(TONIC_WARNINGS)
    target_compile_options(tonic PRIVATE /W3)
  endif()
elseif(TONIC_WARNINGS)
  target_compile_options(tonic PRIVATE -Wall -Wextra -Wno-unknown-pragmas)
endif()

if(APPLE)
  target_link_libraries(tonic PUBLIC "-framework Accelerate" "-framework AudioToolbox" "-framework CoreFoundation")
endif()

find_package(Threads REQUIRED)
target_link_libraries(tonic PUBLIC Threads::Threads)

if(TONIC_BUILD_TESTS)
  enable_testing()
  add_executable(tonic_tests Tests/regression_tests.cpp)
  target_link_libraries(tonic_tests PRIVATE tonic)
  add_test(NAME tonic_regression COMMAND tonic_tests)
endif()

if(TONIC_BUILD_EXAMPLES)
  add_subdirectory(examples/Standalone)
endif()

# Compile-check the demo synth patches so API changes that break them fail CI.
option(TONIC_BUILD_DEMO_SYNTHS "Compile-check examples/Demo Synths" ON)
if(TONIC_BUILD_DEMO_SYNTHS)
  file(GLOB TONIC_DEMO_SYNTHS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/examples/Demo Synths/*.cpp")
  add_library(tonic_demo_synths OBJECT ${TONIC_DEMO_SYNTHS})
  target_link_libraries(tonic_demo_synths PRIVATE tonic)
endif()
