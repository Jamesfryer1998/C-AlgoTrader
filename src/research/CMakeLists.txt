# The research apps' option parsing, runs and printed reports (quant_app, screen_app)
file(GLOB SOURCES CONFIGURE_DEPENDS *.cpp)

add_library(research_lib ${SOURCES})

target_link_libraries(research_lib
    live_lib
    risk_lib
    alpaca_lib
    backtest_lib
    quant_lib
    util_lib
    nlohmann_json)
