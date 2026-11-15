file(GLOB SOURCES CONFIGURE_DEPENDS *.cpp)
add_executable(rebalance_tests ${SOURCES})

target_link_libraries(rebalance_tests
    rebalance_lib
    gtest_main
    nlohmann_json
)

target_include_directories(rebalance_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/tests
)

include(GoogleTest)
gtest_discover_tests(rebalance_tests)
