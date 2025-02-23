file(GLOB SOURCES *.cpp)
add_executable(oms_tests ${SOURCES})

target_link_libraries(oms_tests util_lib)
target_link_libraries(oms_tests oms_lib)
target_link_libraries(oms_tests gtest_main)
target_link_libraries(oms_tests nlohmann_json)

target_include_directories(oms_tests PRIVATE src)

include(GoogleTest)
gtest_discover_tests(oms_tests)