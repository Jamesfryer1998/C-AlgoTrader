add_executable(algo_trader_app main.cpp)
add_executable(optimise_app optimise.cpp)
add_executable(quant_app quant.cpp)
add_executable(screen_app screen.cpp)
add_executable(risk_app risk.cpp)
add_executable(morning_app morning.cpp)
add_executable(pairs_app pairs.cpp)
add_executable(swing_app swing.cpp)
add_executable(intraday_app intraday.cpp)
add_executable(timing_app timing.cpp)
add_executable(momentum_app momentum.cpp)

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

target_link_libraries(morning_app
    morning_lib
    util_lib
    nlohmann_json)

target_link_libraries(pairs_app
    pairs_lib
    util_lib
    nlohmann_json)

target_link_libraries(swing_app
    swing_lib
    util_lib
    nlohmann_json)

target_link_libraries(intraday_app
    intraday_lib
    util_lib
    nlohmann_json)

target_link_libraries(timing_app
    timing_lib
    util_lib
    nlohmann_json)

target_link_libraries(momentum_app
    momentum_lib
    util_lib
    nlohmann_json)
