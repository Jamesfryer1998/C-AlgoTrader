file(GLOB SOURCES *.cpp)
add_executable(util_tests ${SOURCES})

target_link_libraries(util_tests 
    util_lib
    gtest_main
    nlohmann_json
)

# Add include directories for the tests
target_include_directories(util_tests PRIVATE 
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/util
)

include(GoogleTest)
gtest_discover_tests(util_tests)