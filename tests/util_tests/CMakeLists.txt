file(GLOB SOURCES *.cpp)
add_executable(util_tests ${SOURCES})

target_link_libraries(util_tests 
    util_lib
    gtest_main
    nlohmann_json
)

# Add include directories for the tests
target_include_directories(util_tests PRIVATE 
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/util
)

# Ensure test data is copied to build directory if test_data exists
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_data")
    copy_test_data(util_tests "test_data" "test_data")
endif()

include(GoogleTest)
gtest_discover_tests(util_tests)