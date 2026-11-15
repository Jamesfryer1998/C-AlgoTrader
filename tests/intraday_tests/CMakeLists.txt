file(GLOB SOURCES CONFIGURE_DEPENDS *.cpp)
add_executable(intraday_tests ${SOURCES})

target_link_libraries(intraday_tests
    intraday_lib
    gtest_main
    nlohmann_json
)

target_include_directories(intraday_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/tests
)

include(GoogleTest)
gtest_discover_tests(intraday_tests)
