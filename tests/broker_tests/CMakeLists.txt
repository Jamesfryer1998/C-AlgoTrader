file(GLOB SOURCES *.cpp)
add_executable(broker_tests ${SOURCES})

target_link_libraries(broker_tests 
    util_lib
    broker_lib
    oms_lib
    gtest_main
    nlohmann_json
)

# Add include directories for the tests
target_include_directories(broker_tests PRIVATE 
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/broker
    ${CMAKE_SOURCE_DIR}/src/oms
    ${CMAKE_SOURCE_DIR}/src/util
)

include(GoogleTest)
gtest_discover_tests(broker_tests)