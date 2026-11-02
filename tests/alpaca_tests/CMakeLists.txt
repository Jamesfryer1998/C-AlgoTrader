file(GLOB SOURCES *.cpp)
add_executable(alpaca_tests ${SOURCES})

target_link_libraries(alpaca_tests
    alpaca_lib
    oms_lib
    gtest_main
    nlohmann_json
)

target_include_directories(alpaca_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

include(GoogleTest)
gtest_discover_tests(alpaca_tests)
