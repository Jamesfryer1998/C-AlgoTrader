file(GLOB SOURCES CONFIGURE_DEPENDS *.cpp)
add_executable(regression_tests ${SOURCES})

target_link_libraries(regression_tests
    backtester_lib
    broker_lib
    strategy_lib
    oms_lib
    data_access_lib
    util_lib
    gtest_main
    nlohmann_json
)

target_include_directories(regression_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)

include(GoogleTest)
gtest_discover_tests(regression_tests)
