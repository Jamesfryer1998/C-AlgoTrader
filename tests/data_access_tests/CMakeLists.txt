file(GLOB SOURCES *.cpp)
add_executable(data_access_tests ${SOURCES})

target_link_libraries(data_access_tests data_access_lib)
target_link_libraries(data_access_tests gtest_main)

include(GoogleTest)
gtest_discover_tests(data_access_tests)