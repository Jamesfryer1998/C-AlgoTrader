file(GLOB SOURCES *.cpp)
add_executable(test_algo_trader ${SOURCES})

target_link_libraries(test_algo_trader data_access_lib)
target_link_libraries(test_algo_trader gtest_main)

include(GoogleTest)
gtest_discover_tests(test_algo_trader)