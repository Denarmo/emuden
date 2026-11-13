den_module(patching DEPS den_base PRIVATE_DEPS den_xz)

if(TARGET den_patching_tests)
    # Patches written by xdelta3 itself (tests/data/README.md).
    target_compile_definitions(den_patching_tests PRIVATE
        DEN_PATCHING_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
endif()
