# Quant learning projects: Monte Carlo risk, pairs trading, moving average crossover (pure maths, no network)
file(GLOB SOURCES CONFIGURE_DEPENDS *.cpp)

add_library(quant_lib ${SOURCES})

target_link_libraries(quant_lib util_lib)
