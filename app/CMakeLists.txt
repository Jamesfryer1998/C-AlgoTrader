add_executable(algo_trader_app main.cpp)
add_executable(backtest_app backtest.cpp)

target_link_libraries(algo_trader_app 
    alpaca_lib
    util_lib
    data_access_lib
    oms_lib
    strategy_lib
    nlohmann_json)

target_link_libraries(backtest_app 
    alpaca_lib
    util_lib
    data_access_lib
    oms_lib
    strategy_lib
    backtester_lib
    nlohmann_json)