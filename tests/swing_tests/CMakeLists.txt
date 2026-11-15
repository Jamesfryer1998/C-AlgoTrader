file(GLOB SOURCES CONFIGURE_DEPENDS *.cpp)
add_executable(swing_tests ${SOURCES})

target_link_libraries(swing_tests
    swing_lib
    gtest_main
    nlohmann_json
)

target_include_directories(swing_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/tests
)

include(GoogleTest)
gtest_discover_tests(swing_tests)
