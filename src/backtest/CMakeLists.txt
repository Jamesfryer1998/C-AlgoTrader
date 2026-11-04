file(GLOB SOURCES CONFIGURE_DEPENDS *.cpp)
file(GLOB HEADERS CONFIGURE_DEPENDS *.hpp)

# Create a library for the backtester
add_library(backtester_lib ${SOURCES} ${HEADERS})

# Include directories
target_include_directories(backtester_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Link dependencies
target_link_libraries(backtester_lib 
    data_access_lib
    broker_lib
    strategy_lib
    oms_lib
    quant_lib
    util_lib
    nlohmann_json
)