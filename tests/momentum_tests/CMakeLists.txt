file(GLOB SOURCES CONFIGURE_DEPENDS *.cpp)
add_executable(momentum_tests ${SOURCES})

target_link_libraries(momentum_tests
    momentum_lib
    gtest_main
    nlohmann_json
)

target_include_directories(momentum_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/tests
)

include(GoogleTest)
gtest_discover_tests(momentum_tests)
