file(GLOB SOURCES *.cpp)

# Create a library for shared functionality
add_library(oms_lib ${SOURCES})

target_link_libraries(oms_lib nlohmann_json)
target_link_libraries(oms_lib data_access_lib)
target_link_libraries(oms_lib broker_lib)