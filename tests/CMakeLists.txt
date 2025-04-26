# Function to copy test data directories to the build directory
function(copy_test_data target_name source_dir dest_dir)
    add_custom_command(
        TARGET ${target_name} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                ${CMAKE_CURRENT_SOURCE_DIR}/${source_dir}
                ${CMAKE_CURRENT_BINARY_DIR}/${dest_dir}
        COMMENT "Copying test data files for ${target_name}"
    )
endfunction()

add_subdirectory(data_access_tests)
add_subdirectory(util_tests)
add_subdirectory(oms_tests)
add_subdirectory(strategy_tests)
add_subdirectory(broker_tests)
add_subdirectory(backtest_tests)