file(GLOB SOURCES *.cpp)

# Create a library for shared functionality
add_library(oms_lib ${SOURCES})

target_link_libraries(oms_lib 
    nlohmann_json
    data_access_lib
    broker_lib)