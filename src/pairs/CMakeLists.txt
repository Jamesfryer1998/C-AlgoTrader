# Pairs and lead-lag (pairs_app): trading stocks that move together against each other, on daily bars
file(GLOB SOURCES CONFIGURE_DEPENDS *.cpp)

add_library(pairs_lib ${SOURCES})

target_link_libraries(pairs_lib
    morning_lib
    nlohmann_json)
