file(GLOB SOURCES *.cpp)
add_executable(algo_trader_app ${SOURCES})

target_link_libraries(algo_trader_app util_lib)
target_link_libraries(algo_trader_app data_access_lib)
target_link_libraries(algo_trader_app oms_lib)
target_link_libraries(algo_trader_app strategy_lib )
target_link_libraries(algo_trader_app backtester_lib)
target_link_libraries(algo_trader_app nlohmann_json)