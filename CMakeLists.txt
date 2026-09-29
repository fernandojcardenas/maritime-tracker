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
option(MT_TSAN "Build with ThreadSanitizer" OFF)

find_package(Threads REQUIRED)

include(FetchContent)
# JSON parsing for the BarentsWatch live API records.
FetchContent_Declare(nlohmann_json
  URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
  URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa
  DOWNLOAD_EXTRACT_TIMESTAMP ON)
set(JSON_Install OFF CACHE INTERNAL "")
FetchContent_MakeAvailable(nlohmann_json)

add_library(mt_warnings INTERFACE)
target_compile_options(mt_warnings INTERFACE
  -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion -Wnon-virtual-dtor
  $<$<BOOL:${MT_WARNINGS_AS_ERRORS}>:-Werror>)

if(MT_SANITIZE)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
  add_link_options(-fsanitize=address,undefined)
endif()

if(MT_TSAN)
  add_compile_options(-fsanitize=thread -fno-omit-frame-pointer)
  add_link_options(-fsanitize=thread)
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
  src/ais/decoder.cpp
  src/ais/json.cpp
  src/ingest/replay.cpp
  src/ingest/recorder.cpp
  src/ingest/tcp_source.cpp
  src/ingest/fd_source.cpp
  src/track/geo.cpp
  src/track/kalman.cpp
  src/track/tracker.cpp
  src/track/barentswatch.cpp
  src/anomaly/detector.cpp
  src/risk/collision.cpp
  src/spatial/pairs.cpp
  src/serve/websocket.cpp
  src/serve/server.cpp
  src/serve/feed.cpp)
target_include_directories(maritime PUBLIC include)
target_link_libraries(maritime PUBLIC Threads::Threads PRIVATE mt_warnings nlohmann_json::nlohmann_json)

# The live map's files, built into the binary.
set(MT_WEB_FILES index.html app.js style.css land.json vendor/leaflet/leaflet.js vendor/leaflet/leaflet.css
                 vendor/leaflet/LICENSE)
list(TRANSFORM MT_WEB_FILES PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/web/" OUTPUT_VARIABLE MT_WEB_PATHS)
add_custom_command(
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/web_assets.cpp
  COMMAND ${CMAKE_COMMAND} -DWEB_DIR=${CMAKE_CURRENT_SOURCE_DIR}/web -DOUT=${CMAKE_CURRENT_BINARY_DIR}/web_assets.cpp
          -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_web.cmake
  DEPENDS ${MT_WEB_PATHS} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_web.cmake
  COMMENT "Embedding web/ into the binary")
add_library(maritime_web ${CMAKE_CURRENT_BINARY_DIR}/web_assets.cpp)
target_link_libraries(maritime_web PUBLIC maritime)

add_executable(mt-decode apps/mt-decode/main.cpp)
target_link_libraries(mt-decode PRIVATE maritime mt_warnings)

add_executable(mt-track apps/mt-track/main.cpp)
target_link_libraries(mt-track PRIVATE maritime mt_warnings)

add_executable(mt-anomaly apps/mt-anomaly/main.cpp)
target_link_libraries(mt-anomaly PRIVATE maritime mt_warnings)

add_executable(mt-risk apps/mt-risk/main.cpp)
target_link_libraries(mt-risk PRIVATE maritime mt_warnings)

add_executable(mt-bench apps/mt-bench/main.cpp)
target_link_libraries(mt-bench PRIVATE maritime mt_warnings)

add_executable(mt-ingest apps/mt-ingest/main.cpp)
target_link_libraries(mt-ingest PRIVATE maritime maritime_web mt_warnings)

if(MT_BUILD_TESTS)
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
    tests/capture_test.cpp
    tests/ingest_test.cpp
    tests/tcp_source_test.cpp
    tests/track_test.cpp
    tests/barentswatch_test.cpp
    tests/anomaly_test.cpp
    tests/collision_test.cpp
    tests/spatial_test.cpp
    tests/serve_test.cpp)
  target_link_libraries(mt_tests PRIVATE maritime maritime_web GTest::gtest_main nlohmann_json::nlohmann_json)
  target_compile_definitions(mt_tests PRIVATE MT_TESTDATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/testdata"
                                             MT_WEB_DIR="${CMAKE_CURRENT_SOURCE_DIR}/web")
  include(GoogleTest)
  gtest_discover_tests(mt_tests)
endif()

if(MT_BUILD_FUZZERS)
  foreach(target fuzz_decoder fuzz_payload fuzz_framer fuzz_dk_csv fuzz_barentswatch fuzz_anomaly fuzz_collision fuzz_spatial fuzz_websocket)
    add_executable(${target} fuzz/${target}.cpp)
    target_compile_options(${target} PRIVATE -fsanitize=fuzzer)
    target_link_options(${target} PRIVATE -fsanitize=fuzzer)
    target_link_libraries(${target} PRIVATE maritime)
  endforeach()
endif()
