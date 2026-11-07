file(GLOB SOURCES CONFIGURE_DEPENDS *.cpp)
add_executable(risk_tests ${SOURCES})

target_link_libraries(risk_tests
    risk_lib
    gtest_main
    nlohmann_json
)

target_include_directories(risk_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

include(GoogleTest)
gtest_discover_tests(risk_tests)
