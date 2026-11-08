file(GLOB SOURCES CONFIGURE_DEPENDS *.cpp)
add_executable(live_tests ${SOURCES})

target_link_libraries(live_tests
    live_lib
    gtest_main
    nlohmann_json
)

target_include_directories(live_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

include(GoogleTest)
gtest_discover_tests(live_tests)
