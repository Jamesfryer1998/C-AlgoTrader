file(GLOB SOURCES *.cpp)

# Create a library for shared functionality
add_library(data_access_lib ${SOURCES})

target_link_libraries(data_access_lib nlohmann_json)