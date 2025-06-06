file(GLOB SOURCES *.cpp)
add_executable(oms_tests ${SOURCES})

target_link_libraries(oms_tests 
    util_lib
    oms_lib
    gtest_main
    nlohmann_json
)

# Add include directories for the tests
target_include_directories(oms_tests PRIVATE 
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/oms
    ${CMAKE_SOURCE_DIR}/src/util
)

# Ensure test data is copied to build directory if test_data exists
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_data")
    copy_test_data(oms_tests "test_data" "test_data")
endif()

include(GoogleTest)
gtest_discover_tests(oms_tests)