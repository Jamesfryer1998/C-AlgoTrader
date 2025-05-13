cmake_minimum_required(VERSION 3.3)

project(algo_trader)
set(CMAKE_CXX_FLAGS "-g -Wall -Werror -Wextra -Wno-unused-parameter -Wno-unused-private-field -Wno-unused-lambda-capture")
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

FetchContent_MakeAvailable(json)

FetchContent_Declare(
    twsapi
    URL https://interactivebrokers.github.io/downloads/twsapi_macunix.1030.01.zip
)

FetchContent_GetProperties(twsapi)
if(NOT twsapi_POPULATED)
    FetchContent_Populate(twsapi)

    # Define a library target manually
    file(GLOB_RECURSE TWSAPI_SOURCES
    ${twsapi_SOURCE_DIR}/IBJts/source/cppclient/client/*.cpp
    ${twsapi_SOURCE_DIR}/IBJts/source/cppclient/client/*.c
    ${twsapi_SOURCE_DIR}/IBJts/samples/Cpp/TestCppClient/*.cpp)

    # Remove Decimal.cpp
    list(REMOVE_ITEM TWSAPI_SOURCES
        ${twsapi_SOURCE_DIR}/IBJts/source/cppclient/client/Decimal.cpp
        ${twsapi_SOURCE_DIR}/IBJts/samples/Cpp/TestCppClient/Main.cpp
    )
    
    add_library(twsapi STATIC ${TWSAPI_SOURCES})

    target_compile_options(twsapi PRIVATE -Wno-switch)
    
    # Avoid Decimal usage
    target_compile_definitions(twsapi PUBLIC SKIP_DECIMAL)
    # Include all necessary subdirectories
    target_include_directories(twsapi PUBLIC
    ${twsapi_SOURCE_DIR}/IBJts/source/cppclient
    ${twsapi_SOURCE_DIR}/IBJts/source/cppclient/client
    ${twsapi_SOURCE_DIR}/IBJts/samples/Cpp/TestCppClient)

endif()
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