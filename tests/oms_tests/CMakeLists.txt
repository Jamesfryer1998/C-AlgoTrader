file(GLOB SOURCES *.cpp)
add_executable(oms_tests ${SOURCES})

target_link_libraries(oms_tests 
    util_lib
    oms_lib
    gtest_main
    nlohmann_json
)

# Add include directories for the tests
target_include_directories(oms_tests PRIVATE 
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/oms
    ${CMAKE_SOURCE_DIR}/src/util
)

include(GoogleTest)
gtest_discover_tests(oms_tests)