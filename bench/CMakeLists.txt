function(tda_add_bench target_name source_file)
    add_executable(${target_name} ${source_file})

    target_link_libraries(${target_name} PRIVATE tda::tda)
    target_include_directories(${target_name} PRIVATE ${PROJECT_SOURCE_DIR}/thirdparty/ubench)
    if (UNIX)
        target_link_libraries(${target_name} PRIVATE m)
    endif ()
    target_compile_options(${target_name} PRIVATE ${TDA_WARNINGS})

    set_target_properties(${target_name} PROPERTIES C_EXTENSIONS ON)
endfunction()

tda_add_bench(bench_algo_sort algo/bench_sort.c)
tda_add_bench(bench_alloc alloc/bench_alloc.c)
tda_add_bench(bench_ds_hmap ds/bench_hmap.c)
tda_add_bench(bench_ds_vec ds/bench_vec.c)
