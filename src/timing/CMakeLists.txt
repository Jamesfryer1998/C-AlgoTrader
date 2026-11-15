# Market timing (timing_app): thousands of rules for when to hold SPY, on daily bars
file(GLOB SOURCES CONFIGURE_DEPENDS *.cpp)

add_library(timing_lib ${SOURCES})

target_link_libraries(timing_lib
    swing_lib
    nlohmann_json)
