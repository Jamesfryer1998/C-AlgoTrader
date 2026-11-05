add_executable(algo_trader_app main.cpp)
add_executable(backtest_app backtest.cpp)
add_executable(optimise_app optimise.cpp)
add_executable(quant_app quant.cpp)
add_executable(screen_app screen.cpp)

target_link_libraries(algo_trader_app
    live_lib
    util_lib
    nlohmann_json)

target_link_libraries(backtest_app 
    alpaca_lib
    util_lib
    data_access_lib
    oms_lib
    strategy_lib
    backtester_lib
    nlohmann_json)

target_link_libraries(optimise_app
    alpaca_lib
    util_lib
    data_access_lib
    oms_lib
    strategy_lib
    backtester_lib
    nlohmann_json)

target_link_libraries(quant_app
    research_lib
    util_lib
    nlohmann_json)

target_link_libraries(screen_app
    alpaca_lib
    backtester_lib
    quant_lib
    util_lib
    nlohmann_json)
