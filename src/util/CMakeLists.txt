file(GLOB SOURCES *.cpp)

include(FetchContent)

# Create a library for shared functionality
add_library(util_lib ${SOURCES})

# Link the nlohmann JSON library to your util_lib
target_link_libraries(util_lib nlohmann_json)