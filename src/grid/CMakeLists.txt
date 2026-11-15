# Buying drops and selling pops (grid_app): trimming and adding to positions after big days
file(GLOB SOURCES CONFIGURE_DEPENDS *.cpp)

add_library(grid_lib ${SOURCES})

target_link_libraries(grid_lib
    pairs_lib
    nlohmann_json)
