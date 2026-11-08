add_executable(algo_trader_app main.cpp)
add_executable(optimise_app optimise.cpp)
add_executable(quant_app quant.cpp)
add_executable(screen_app screen.cpp)
add_executable(risk_app risk.cpp)

target_link_libraries(algo_trader_app
    live_lib
    util_lib
    nlohmann_json)

target_link_libraries(optimise_app
    alpaca_lib
    util_lib
    data_access_lib
    oms_lib
    strategy_lib
    backtest_lib
    nlohmann_json)

target_link_libraries(quant_app
    research_lib
    util_lib
    nlohmann_json)

target_link_libraries(screen_app
    research_lib
    util_lib
    nlohmann_json)

target_link_libraries(risk_app
    research_lib
    util_lib
    nlohmann_json)
