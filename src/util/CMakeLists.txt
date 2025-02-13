file(GLOB SOURCES *.cpp)

include(FetchContent)

# Create a library for shared functionality
add_library(util_lib ${SOURCES})

# Link the nlohmann JSON library to your util_lib
target_link_libraries(util_lib PRIVATE nlohmann_json::nlohmann_json)

# And cerate libaries for testing
#add_executable(util ${SOURCES})
#target_link_libraries(util PRIVATE util_lib)
#target_link_libraries(util PRIVATE nlohmann_json::nlohmann_json)