den_module(library DEPS den_base den_platform den_store den_patching PRIVATE_DEPS den_gfx)

# The download tests check the magic of the bundled console manifests (assets/plugins).
if(TARGET den_library_tests)
    target_compile_definitions(den_library_tests PRIVATE DEN_BUNDLED_PLUGINS="${CMAKE_SOURCE_DIR}/assets/plugins")
endif()
