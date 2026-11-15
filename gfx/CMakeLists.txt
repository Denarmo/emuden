den_module(gfx DEPS den_base PRIVATE_DEPS freetype den_stb glslang SPIRV glslang-default-resource-limits
    spirv-cross-glsl spirv-cross-core)

# Vulkan headers (no loader is linked: Vulkan is loaded at run time) from SDL's Khronos copy.
target_include_directories(den_gfx SYSTEM PUBLIC "${sdl3_SOURCE_DIR}/src/video/khronos")
if(ANDROID)
    target_link_libraries(den_gfx PRIVATE EGL android)
endif()
