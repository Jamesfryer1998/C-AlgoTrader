# The research apps' option parsing, runs and printed reports (quant_app, screen_app)
file(GLOB SOURCES CONFIGURE_DEPENDS *.cpp)

add_library(research_lib ${SOURCES})

target_link_libraries(research_lib
    alpaca_lib
    backtester_lib
    quant_lib
    util_lib
    nlohmann_json)
