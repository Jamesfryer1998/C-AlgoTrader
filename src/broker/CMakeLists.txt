file(GLOB SOURCES CONFIGURE_DEPENDS *.cpp)

# Create a library for shared functionality
add_library(broker_lib ${SOURCES})

# Brokers use Position from oms_lib, and oms_lib uses BrokerBase: telling CMake about both directions lets
# it repeat them in the right order when linking static libraries
target_link_libraries(broker_lib data_access_lib oms_lib)