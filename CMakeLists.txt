cmake_minimum_required(VERSION 3.14)

project(algo_trader)
set(CMAKE_CXX_STANDARD 23)

# Install gtest which now lives on head.
cmake_policy(SET CMP0135 NEW)
include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
)
FetchContent_MakeAvailable(googletest)

# Declare and fetch the json library
FetchContent_Declare(
    json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
)

# Make json available
FetchContent_MakeAvailable(json)

# IXWebSocket: small C++ WebSocket + HTTP client, used to talk to Alpaca.
# TLS: macOS uses Apple's built-in TLS (nothing to install). Linux uses OpenSSL (libssl-dev).
set(USE_TLS ON CACHE BOOL "" FORCE)
set(USE_ZLIB OFF CACHE BOOL "" FORCE)
set(IXWEBSOCKET_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    ixwebsocket
    URL https://github.com/machinezone/IXWebSocket/archive/refs/tags/v11.4.6.tar.gz
)
FetchContent_MakeAvailable(ixwebsocket)

# Our warning flags are set AFTER fetching third-party code so -Werror only applies to our code.
set(CMAKE_CXX_FLAGS "-g -Wall -Werror -Wextra -Wno-unused-parameter -Wno-unused-private-field -Wno-unused-lambda-capture")

# Setup CMAKE Module path for other conda packages
SET(CMAKE_MODULE_PATH $ENV{$CONDA_PREFIX}/lib/cmake)
if(DEFINED ENV{CONDA_PREFIX} AND NOT DEFINED ENV{CONDA_BUILD})
  message("Setting CMAKE_FIND_ROOT_PATH")
  set(CMAKE_FIND_ROOT_PATH $ENV{CONDA_PREFIX})
endif()

enable_testing()
add_subdirectory(src)
add_subdirectory(app)
add_subdirectory(tests)
