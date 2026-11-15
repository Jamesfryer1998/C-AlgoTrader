# Intraday statistical arbitrage (intraday_app): many hedged trades a day, on minute bars
file(GLOB SOURCES CONFIGURE_DEPENDS *.cpp)

add_library(intraday_lib ${SOURCES})

target_link_libraries(intraday_lib
    pairs_lib
    nlohmann_json)
