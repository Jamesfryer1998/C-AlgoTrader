file(GLOB SOURCES *.cpp)
add_executable(broker_tests ${SOURCES})

target_link_libraries(broker_tests 
    util_lib
    broker_lib
    oms_lib
    gtest_main
    nlohmann_json
)

# Add include directories for the tests
target_include_directories(broker_tests PRIVATE 
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/broker
    ${CMAKE_SOURCE_DIR}/src/oms
    ${CMAKE_SOURCE_DIR}/src/util
)

# Ensure test data is copied to build directory if test_data exists
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_data")
    copy_test_data(broker_tests "test_data" "test_data")
endif()

include(GoogleTest)
gtest_discover_tests(broker_tests)