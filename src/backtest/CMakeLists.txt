file(GLOB SOURCES *.cpp)

# Create a library for shared functionality
add_library(backtester_lib ${SOURCES})

target_link_libraries(backtester_lib data_access_lib)
target_link_libraries(backtester_lib broker_lib)
target_link_libraries(backtester_lib nlohmann_json)
