# Position sizing: how many shares each order is for (see RiskEngine)
file(GLOB SOURCES CONFIGURE_DEPENDS *.cpp)

add_library(risk_lib ${SOURCES})

target_link_libraries(risk_lib
    oms_lib
    data_access_lib
    util_lib
    nlohmann_json)
