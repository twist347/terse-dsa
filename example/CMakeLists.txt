function(tda_add_example target_name source_file)
    add_executable(${target_name} ${source_file})

    target_link_libraries(${target_name} PRIVATE tda::tda)
    target_compile_options(${target_name} PRIVATE ${TDA_WARNINGS})

    add_test(NAME ${target_name} COMMAND ${target_name})
    set_tests_properties(${target_name} PROPERTIES TIMEOUT ${TDA_TEST_TIMEOUT})
endfunction()

add_subdirectory(algo)
add_subdirectory(alloc)
add_subdirectory(core)
add_subdirectory(ds)
