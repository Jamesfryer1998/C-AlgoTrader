file(GLOB SOURCES *.cpp)
add_executable(data_access_tests ${SOURCES})

target_link_libraries(data_access_tests 
    data_access_lib
    util_lib
    gtest_main
    nlohmann_json
)

# Add include directories for the tests
target_include_directories(data_access_tests PRIVATE 
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/data_access
    ${CMAKE_SOURCE_DIR}/src/util
)

# Copy test data to build directory
copy_test_data(data_access_tests "test_data" "test_data")

include(GoogleTest)
gtest_discover_tests(data_access_tests)