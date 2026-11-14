file(GLOB SOURCES CONFIGURE_DEPENDS *.cpp)
add_executable(morning_tests ${SOURCES})

target_link_libraries(morning_tests
    morning_lib
    gtest_main
    nlohmann_json
)

target_include_directories(morning_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/tests
)

include(GoogleTest)
gtest_discover_tests(morning_tests)
