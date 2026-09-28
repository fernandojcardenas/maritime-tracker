cmake_minimum_required(VERSION 3.24)
project(maritime_tracker VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(MT_BUILD_TESTS "Build unit tests" ON)
option(MT_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(MT_BUILD_FUZZERS "Build libFuzzer targets (requires Clang)" OFF)
option(MT_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

add_library(mt_warnings INTERFACE)
target_compile_options(mt_warnings INTERFACE
  -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion -Wnon-virtual-dtor
  $<$<BOOL:${MT_WARNINGS_AS_ERRORS}>:-Werror>)

if(MT_SANITIZE)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
  add_link_options(-fsanitize=address,undefined)
endif()

if(MT_BUILD_FUZZERS)
  # Coverage instrumentation for everything; the fuzzer main is linked only into the fuzz target.
  add_compile_options(-fsanitize=fuzzer-no-link,address,undefined)
  add_link_options(-fsanitize=address,undefined)
endif()

add_library(maritime
  src/nmea/sentence.cpp
  src/ais/bits.cpp
  src/ais/messages.cpp
  src/ais/decoder.cpp)
target_include_directories(maritime PUBLIC include)
target_link_libraries(maritime PRIVATE mt_warnings)

add_executable(mt-decode apps/mt-decode/main.cpp)
target_link_libraries(mt-decode PRIVATE maritime mt_warnings)

if(MT_BUILD_TESTS)
  include(FetchContent)
  FetchContent_Declare(googletest
    URL https://github.com/google/googletest/releases/download/v1.17.0/googletest-1.17.0.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP ON)
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)

  enable_testing()
  add_executable(mt_tests
    tests/sentence_test.cpp
    tests/bits_test.cpp
    tests/messages_test.cpp
    tests/decoder_test.cpp
    tests/capture_test.cpp)
  target_link_libraries(mt_tests PRIVATE maritime GTest::gtest_main)
  target_compile_definitions(mt_tests PRIVATE MT_TESTDATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/testdata")
  include(GoogleTest)
  gtest_discover_tests(mt_tests)
endif()

if(MT_BUILD_FUZZERS)
  foreach(target fuzz_decoder fuzz_payload)
    add_executable(${target} fuzz/${target}.cpp)
    target_compile_options(${target} PRIVATE -fsanitize=fuzzer)
    target_link_options(${target} PRIVATE -fsanitize=fuzzer)
    target_link_libraries(${target} PRIVATE maritime)
  endforeach()
endif()
