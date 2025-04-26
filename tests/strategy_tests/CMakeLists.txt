file(GLOB SOURCES *.cpp)
add_executable(strategy_tests ${SOURCES})

target_link_libraries(strategy_tests 
    util_lib
    strategy_lib
    broker_lib
    gtest_main
    nlohmann_json
)

# Add include directories for the tests
target_include_directories(strategy_tests PRIVATE 
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/strategy_engine
    ${CMAKE_SOURCE_DIR}/src/broker
    ${CMAKE_SOURCE_DIR}/src/util
)

# Copy test data to build directory
copy_test_data(strategy_tests "test_data" "test_data")

include(GoogleTest)
gtest_discover_tests(strategy_tests)