den_module(system DEPS den_base den_platform den_store)
target_compile_definitions(den_system PRIVATE
    DEN_RELEASE_VERSION="${EMUDEN_VERSION}" DEN_PROJECT_VERSION="${PROJECT_VERSION}")

# The device tests check the tuning of the bundled console manifests (assets/plugins).
if(TARGET den_system_tests)
    target_compile_definitions(den_system_tests PRIVATE DEN_BUNDLED_PLUGINS="${CMAKE_SOURCE_DIR}/assets/plugins")
endif()
