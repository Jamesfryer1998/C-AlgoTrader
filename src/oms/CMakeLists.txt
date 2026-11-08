file(GLOB SOURCES CONFIGURE_DEPENDS *.cpp)

# Create a library for shared functionality
add_library(oms_lib ${SOURCES})

# TradeStore writes files on a background thread
find_package(Threads REQUIRED)

target_link_libraries(oms_lib 
    nlohmann_json
    data_access_lib
    broker_lib
    Threads::Threads)