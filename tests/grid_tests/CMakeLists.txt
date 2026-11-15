file(GLOB SOURCES CONFIGURE_DEPENDS *.cpp)
add_executable(grid_tests ${SOURCES})

target_link_libraries(grid_tests
    grid_lib
    gtest_main
    nlohmann_json
)

target_include_directories(grid_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/tests
)

include(GoogleTest)
gtest_discover_tests(grid_tests)
