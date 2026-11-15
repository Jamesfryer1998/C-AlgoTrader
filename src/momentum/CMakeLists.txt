# Intraday momentum (momentum_app): the published noise-area breakout on SPY, QQQ and IWM, on minute bars
file(GLOB SOURCES CONFIGURE_DEPENDS *.cpp)

add_library(momentum_lib ${SOURCES})

target_link_libraries(momentum_lib
    morning_lib
    nlohmann_json)
