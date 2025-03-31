file(GLOB SOURCES *.cpp)
file(GLOB HEADERS *.hpp)

# Create a library for shared functionality
add_library(data_access_lib ${SOURCES} ${HEADERS})

# Add include directories
target_include_directories(data_access_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Link dependencies
target_link_libraries(data_access_lib 
    util_lib
    nlohmann_json
)