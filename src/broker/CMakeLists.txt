file(GLOB SOURCES *.cpp)

# Create a library for shared functionality
add_library(broker_lib ${SOURCES})

target_link_libraries(broker_lib data_access_lib)