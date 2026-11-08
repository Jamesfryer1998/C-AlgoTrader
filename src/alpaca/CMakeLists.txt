# Everything that talks to Alpaca (market data stream + order REST API) lives in this folder.
file(GLOB SOURCES CONFIGURE_DEPENDS *.cpp)

add_library(alpaca_lib ${SOURCES})

target_link_libraries(alpaca_lib
    ixwebsocket
    broker_lib
    data_access_lib
    quant_lib
    util_lib
    nlohmann_json
)
