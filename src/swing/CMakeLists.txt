# Swing trading (swing_app): positions held days to weeks, on daily bars
file(GLOB SOURCES CONFIGURE_DEPENDS *.cpp)

add_library(swing_lib ${SOURCES})

target_link_libraries(swing_lib
    pairs_lib
    nlohmann_json)
