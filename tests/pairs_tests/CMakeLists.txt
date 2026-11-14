file(GLOB SOURCES CONFIGURE_DEPENDS *.cpp)
add_executable(pairs_tests ${SOURCES})

target_link_libraries(pairs_tests
    pairs_lib
    gtest_main
    nlohmann_json
)

target_include_directories(pairs_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/tests
)

include(GoogleTest)
gtest_discover_tests(pairs_tests)
