# Quant learning projects: Monte Carlo risk, pairs trading, moving average crossover (pure maths, no network)
file(GLOB SOURCES *.cpp)

add_library(quant_lib ${SOURCES})
