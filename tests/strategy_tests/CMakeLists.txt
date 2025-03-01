file(GLOB SOURCES *.cpp)
add_executable(strategy_tests ${SOURCES})

target_link_libraries(strategy_tests util_lib)
target_link_libraries(strategy_tests strategy_lib)
target_link_libraries(strategy_tests gtest_main)
target_link_libraries(strategy_tests nlohmann_json)

target_include_directories(strategy_tests PRIVATE src)

include(GoogleTest)
gtest_discover_tests(strategy_tests)