# The morning screen (morning_app): each day's stocks in play, the opening range breakout, and the verdicts
file(GLOB SOURCES CONFIGURE_DEPENDS *.cpp)

add_library(morning_lib ${SOURCES})

target_link_libraries(morning_lib
    backtest_lib
    risk_lib
    alpaca_lib
    quant_lib
    util_lib
    nlohmann_json)
