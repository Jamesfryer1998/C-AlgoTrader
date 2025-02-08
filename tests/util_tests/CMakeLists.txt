file(GLOB SOURCES *.cpp)
add_executable(util_tests ${SOURCES})

target_link_libraries(util_tests util_lib)
target_link_libraries(util_tests gtest_main)

target_link_libraries(util_tests nlohmann_json)

include(GoogleTest)
gtest_discover_tests(util_tests)