# The live trading session: one lane per stock, the per-minute table, live (Alpaca) or replay (saved bars)
file(GLOB SOURCES CONFIGURE_DEPENDS *.cpp)

add_library(live_lib ${SOURCES})

target_link_libraries(live_lib
    risk_lib
    alpaca_lib
    backtester_lib
    strategy_lib
    oms_lib
    broker_lib
    util_lib
    nlohmann_json)
