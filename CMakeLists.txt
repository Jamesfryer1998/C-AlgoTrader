cmake_minimum_required(VERSION 3.3)

project(algo_trader)
set(CMAKE_CXX_FLAGS "-g -Wall -Werror -Wextra -Wno-unused-parameter -Wno-unused-private-field")
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