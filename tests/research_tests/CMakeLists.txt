file(GLOB SOURCES CONFIGURE_DEPENDS *.cpp)
add_executable(research_tests ${SOURCES})

target_link_libraries(research_tests
    research_lib
    gtest_main
    nlohmann_json
)

target_include_directories(research_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

include(GoogleTest)
gtest_discover_tests(research_tests)
