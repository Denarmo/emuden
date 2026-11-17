den_module(base PRIVATE_DEPS zlibstatic den_lzma)
target_compile_definitions(den_base PRIVATE DEN_HAVE_ZLIB)
# The 7z tests set the reader's limits (src/seven_zip.h) and pack with the SDK's encoder.
if(TARGET den_base_tests)
    target_include_directories(den_base_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(den_base_tests PRIVATE den_lzma_encoder)
endif()
