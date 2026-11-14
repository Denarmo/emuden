den_module(engine DEPS den_base den_platform den_store)
if(TARGET den_engine_tests)
    target_include_directories(den_engine_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
endif()
