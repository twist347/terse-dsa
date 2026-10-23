tda_add_test(test_alloc_alloc test_alloc.c)
tda_add_test(test_alloc_arena test_arena.c)
tda_add_test(test_alloc_debug test_debug.c)
tda_add_test(test_alloc_default test_default.c)
tda_add_test(test_alloc_log test_log.c)
tda_add_test(test_alloc_pool test_pool.c)
tda_add_test(test_alloc_slab test_slab.c)

# each misuse aborts, so each runs as its own process and passes on its report line
add_executable(test_alloc_debug_misuse test_debug_misuse.c)
target_link_libraries(test_alloc_debug_misuse PRIVATE tda::tda)
target_compile_options(test_alloc_debug_misuse PRIVATE ${TDA_WARNINGS})

set(debug_misuse_cases
    "wrong_size|made with 32 bytes at 8, given back with 24 at 8"
    "wrong_align|made with 32 bytes at 64, given back with 32 at 8"
    "realloc_wrong_size|made with 8 bytes at 8, resized with 64 at 8"
    "realloc_wrong_align|made with 64 bytes at 64, resized with 64 at 8"
    "double_free|freed twice"
    "foreign_pointer|was not handed out by this allocator"
    "write_past_end|was written past its end"
    "write_after_free|was written after it was freed"
    "stale_realloc|was written after it was freed"
    "break_at|break at block #2"
)
foreach(entry IN LISTS debug_misuse_cases)
    string(REPLACE "|" ";" parts "${entry}")
    list(GET parts 0 name)
    list(GET parts 1 report)
    add_test(NAME test_alloc_debug_misuse_${name} COMMAND test_alloc_debug_misuse ${name})
    set_tests_properties(test_alloc_debug_misuse_${name} PROPERTIES
        PASS_REGULAR_EXPRESSION "tda debug: .*${report}")
endforeach()
