tda_add_test(test_core_cmp test_cmp.c)
tda_add_test(test_core_hash test_hash.c)
tda_add_test(test_core_layout test_layout.c)
tda_add_test(test_core_print test_print.c)
tda_add_test(test_core_rng test_rng.c)
tda_add_test(test_core_span test_span.c)
tda_add_test(test_core_status test_status.c)
tda_add_test(test_core_util test_util.c)

# test_rng.c calls nextafter itself, and the library keeps its own libm private
if (UNIX)
    target_link_libraries(test_core_rng PRIVATE m)
endif ()
