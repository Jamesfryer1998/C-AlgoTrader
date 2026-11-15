file(GLOB SOURCES CONFIGURE_DEPENDS *.cpp)
add_executable(timing_tests ${SOURCES})

target_link_libraries(timing_tests
    timing_lib
    gtest_main
    nlohmann_json
)

target_include_directories(timing_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/tests
)

include(GoogleTest)
gtest_discover_tests(timing_tests)
