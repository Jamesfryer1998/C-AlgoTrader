# Month-end rebalancing (rebalance_app): front-running pension funds' 60/40 rebalancing, on daily bars
file(GLOB SOURCES CONFIGURE_DEPENDS *.cpp)

add_library(rebalance_lib ${SOURCES})

target_link_libraries(rebalance_lib
    morning_lib
    nlohmann_json)
