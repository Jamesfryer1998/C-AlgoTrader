file(GLOB SOURCES *.cpp)
add_executable(broker_tests ${SOURCES})

target_link_libraries(broker_tests util_lib)
target_link_libraries(broker_tests broker_lib)
target_link_libraries(broker_tests oms_lib)
target_link_libraries(broker_tests gtest_main)
target_link_libraries(broker_tests nlohmann_json)

target_include_directories(broker_tests PRIVATE src)

include(GoogleTest)
gtest_discover_tests(broker_tests)