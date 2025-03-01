file(GLOB SOURCES *.cpp)

# Create a library for shared functionality
add_library(strategy_lib ${SOURCES})

target_link_libraries(strategy_lib nlohmann_json)