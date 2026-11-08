file(GLOB SOURCES CONFIGURE_DEPENDS *.cpp)
add_executable(quant_tests ${SOURCES})

target_link_libraries(quant_tests
    quant_lib
    gtest_main
)

target_include_directories(quant_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

include(GoogleTest)
gtest_discover_tests(quant_tests)
