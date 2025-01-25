file(GLOB SOURCES *.cpp)

# Create a library for shared functionality
add_library(data_access_lib ${SOURCES})

# And cerate libaries for testing
# add_executable(data_access ${SOURCES})
# target_link_libraries(data_access PRIVATE data_access_lib)