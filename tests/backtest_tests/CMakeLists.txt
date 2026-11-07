file(GLOB SOURCES CONFIGURE_DEPENDS *.cpp)
add_executable(backtest_tests ${SOURCES})

target_link_libraries(backtest_tests 
    backtest_lib
    quant_lib
    broker_lib
    strategy_lib
    oms_lib
    data_access_lib
    util_lib
    gtest_main
    nlohmann_json
)

# Add include directories for the tests
target_include_directories(backtest_tests PRIVATE 
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/backtest
    ${CMAKE_SOURCE_DIR}/src/broker
    ${CMAKE_SOURCE_DIR}/src/data_access
    ${CMAKE_SOURCE_DIR}/src/strategy_engine
    ${CMAKE_SOURCE_DIR}/src/oms
    ${CMAKE_SOURCE_DIR}/src/util
)

# Copy test data to build directory (only if the folder exists)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_data")
    copy_test_data(backtest_tests "test_data" "test_data")
endif()

# Enable verbose output for GoogleTest
set(CMAKE_CTEST_ARGUMENTS "--verbose")

include(GoogleTest)
gtest_discover_tests(backtest_tests)