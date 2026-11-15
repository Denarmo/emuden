# Third-party sources live once in .deps and are shared by every build directory.
# A source tree that is already there is used as it is, so parallel builds never fetch.
include(FetchContent)
set(DEN_DEPS_DIR "${CMAKE_SOURCE_DIR}/.deps" CACHE PATH "Shared dependency sources")
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

function(den_fetch name repo tag)
    string(TOUPPER ${name} upper)
    set(src "${DEN_DEPS_DIR}/${name}-src")
    if(EXISTS "${src}/.den-ready-${tag}")
        set(FETCHCONTENT_SOURCE_DIR_${upper} "${src}" CACHE PATH "" FORCE)
    endif()
    FetchContent_Declare(${name} GIT_REPOSITORY ${repo} GIT_TAG ${tag}
        SOURCE_DIR "${src}" EXCLUDE_FROM_ALL ${ARGN})
    FetchContent_MakeAvailable(${name})
    file(TOUCH "${src}/.den-ready-${tag}")
    set(${name}_SOURCE_DIR "${${name}_SOURCE_DIR}" PARENT_SCOPE)
    set(${name}_BINARY_DIR "${${name}_BINARY_DIR}" PARENT_SCOPE)
endfunction()

set(BUILD_SHARED_LIBS OFF)

set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_CAMERA OFF CACHE BOOL "" FORCE)
set(SDL_GPU OFF CACHE BOOL "" FORCE)
set(SDL_RENDER OFF CACHE BOOL "" FORCE)
den_fetch(sdl3 https://github.com/libsdl-org/SDL.git release-3.2.16 GIT_SHALLOW ON)

set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
den_fetch(zlib https://github.com/madler/zlib.git v1.3.1 GIT_SHALLOW ON)
target_include_directories(zlibstatic PUBLIC "${zlib_SOURCE_DIR}" "${zlib_BINARY_DIR}")

set(FT_DISABLE_ZLIB ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)
den_fetch(freetype https://github.com/freetype/freetype.git VER-2-13-3 GIT_SHALLOW ON)

# stb has no releases; this is a pinned commit.
den_fetch(stb https://github.com/nothings/stb.git 5736b15f7ea0ffb08dd38af21067c314d6a3aae9)
add_library(den_stb INTERFACE)
target_include_directories(den_stb SYSTEM INTERFACE "${stb_SOURCE_DIR}")

# QR codes on plugin pages (a sign-in link to open on a phone).
den_fetch(qrcodegen https://github.com/nayuki/QR-Code-generator.git v1.8.0 GIT_SHALLOW ON)
add_library(den_qrcodegen STATIC "${qrcodegen_SOURCE_DIR}/cpp/qrcodegen.cpp")
target_include_directories(den_qrcodegen SYSTEM PUBLIC "${qrcodegen_SOURCE_DIR}/cpp")

if(DEN_BUILD_TESTS)
    den_fetch(catch2 https://github.com/catchorg/Catch2.git v3.8.1 GIT_SHALLOW ON)
endif()

# Lua 5.4 runs feature plugins (plugins/src/feature). It is built as C++ so that a script
# error unwinds the host's frames as an exception instead of jumping over them.
den_fetch(lua https://github.com/lua/lua.git v5.4.8 GIT_SHALLOW ON)
file(GLOB den_lua_sources "${lua_SOURCE_DIR}/l*.c")
list(FILTER den_lua_sources EXCLUDE REGEX "/(lua|ltests|onelua)\.c$")
add_library(den_lua STATIC ${den_lua_sources})
set_source_files_properties(${den_lua_sources} PROPERTIES LANGUAGE CXX)
target_include_directories(den_lua SYSTEM PUBLIC "${lua_SOURCE_DIR}")
if(MSVC)
    target_compile_options(den_lua PRIVATE /w /EHsc)
else()
    target_compile_options(den_lua PRIVATE -w)
endif()

if(WIN32)
    # The WebView2 SDK: headers and the static loader from Microsoft's NuGet package (a zip).
    # The engine itself is the Evergreen runtime that ships with Windows.
    set(DEN_WEBVIEW2_VERSION 1.0.2903.40)
    set(webview2_src "${DEN_DEPS_DIR}/webview2-src")
    if(NOT EXISTS "${webview2_src}/.den-ready-${DEN_WEBVIEW2_VERSION}")
        set(webview2_zip "${DEN_DEPS_DIR}/webview2-${DEN_WEBVIEW2_VERSION}.zip")
        file(DOWNLOAD
            "https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/${DEN_WEBVIEW2_VERSION}"
            "${webview2_zip}"
            EXPECTED_HASH SHA256=ef128016dd1e51c59178c827ed5b8aa3322c57afa8675d930f8109505542ad74
            TLS_VERIFY ON STATUS webview2_status)
        list(GET webview2_status 0 webview2_code)
        if(NOT webview2_code EQUAL 0)
            message(FATAL_ERROR "WebView2 SDK download failed: ${webview2_status}")
        endif()
        file(ARCHIVE_EXTRACT INPUT "${webview2_zip}" DESTINATION "${webview2_src}"
            PATTERNS "build/native/include/*" "build/native/*/WebView2LoaderStatic.lib" "LICENSE.txt")
        file(REMOVE "${webview2_zip}")
        file(TOUCH "${webview2_src}/.den-ready-${DEN_WEBVIEW2_VERSION}")
    endif()
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "ARM64|aarch64")
        set(webview2_arch arm64)
    elseif(CMAKE_SIZEOF_VOID_P EQUAL 4)
        set(webview2_arch x86)
    else()
        set(webview2_arch x64)
    endif()
    add_library(den_webview2 INTERFACE)
    target_include_directories(den_webview2 SYSTEM INTERFACE "${webview2_src}/build/native/include")
    target_link_libraries(den_webview2 INTERFACE
        "${webview2_src}/build/native/${webview2_arch}/WebView2LoaderStatic.lib"
        advapi32 ole32 oleaut32 shell32 shlwapi user32 gdi32 version)
endif()

# XZ Embedded (0BSD) decodes the LZMA sections of xdelta patches (patching/src). It is a
# decoder only; the three files below are all of it that is built.
den_fetch(xzembedded https://github.com/tukaani-project/xz-embedded.git v2024-12-30 GIT_SHALLOW ON)
add_library(den_xz STATIC
    "${xzembedded_SOURCE_DIR}/linux/lib/xz/xz_crc32.c"
    "${xzembedded_SOURCE_DIR}/linux/lib/xz/xz_dec_lzma2.c"
    "${xzembedded_SOURCE_DIR}/linux/lib/xz/xz_dec_stream.c")
target_include_directories(den_xz SYSTEM PUBLIC "${xzembedded_SOURCE_DIR}/linux/include/linux"
    PRIVATE "${xzembedded_SOURCE_DIR}/userspace")
set_target_properties(den_xz PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(MSVC)
    target_compile_options(den_xz PRIVATE /w)
else()
    target_compile_options(den_xz PRIVATE -w)
endif()

# The LZMA SDK (public domain, Igor Pavlov) reads 7z archives (base/src/seven_zip.cpp): the
# container, LZMA, LZMA2, the BCJ and delta filters. Decoder files only; no encoder is built.
set(DEN_LZMA_SDK_VERSION 2501)
set(lzma_sdk_src "${DEN_DEPS_DIR}/lzma-sdk-src")
if(NOT EXISTS "${lzma_sdk_src}/.den-ready-${DEN_LZMA_SDK_VERSION}")
    set(lzma_sdk_archive "${DEN_DEPS_DIR}/lzma${DEN_LZMA_SDK_VERSION}.7z")
    file(DOWNLOAD "https://www.7-zip.org/a/lzma${DEN_LZMA_SDK_VERSION}.7z" "${lzma_sdk_archive}"
        EXPECTED_HASH SHA256=cbc3babd589d971e45971d787ff100be8aaa5eab15b2694497ec3e447009e1f2
        TLS_VERIFY ON STATUS lzma_sdk_status)
    list(GET lzma_sdk_status 0 lzma_sdk_code)
    if(NOT lzma_sdk_code EQUAL 0)
        message(FATAL_ERROR "LZMA SDK download failed: ${lzma_sdk_status}")
    endif()
    file(ARCHIVE_EXTRACT INPUT "${lzma_sdk_archive}" DESTINATION "${lzma_sdk_src}"
        PATTERNS "C/*.c" "C/*.h" "DOC/lzma-sdk.txt")
    file(REMOVE "${lzma_sdk_archive}")
    file(TOUCH "${lzma_sdk_src}/.den-ready-${DEN_LZMA_SDK_VERSION}")
endif()
set(den_lzma_sources)
foreach(name 7zArcIn 7zBuf 7zCrc 7zCrcOpt 7zDec 7zStream Bcj2 Bra Bra86 BraIA64 CpuArch Delta
        LzmaDec Lzma2Dec)
    list(APPEND den_lzma_sources "${lzma_sdk_src}/C/${name}.c")
endforeach()
add_library(den_lzma STATIC ${den_lzma_sources})
target_include_directories(den_lzma SYSTEM PUBLIC "${lzma_sdk_src}/C")
set_target_properties(den_lzma PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(MSVC)
    target_compile_options(den_lzma PRIVATE /w)
else()
    target_compile_options(den_lzma PRIVATE -w)
endif()

# glslang and SPIRV-Cross turn imported Slang shaders (Vulkan GLSL) into this device's GLSL
# (gfx/src/retro_compile.cpp): Khronos' compiler to SPIR-V and back, without SPIRV-Tools.
set(ENABLE_OPT OFF CACHE BOOL "" FORCE)
set(ENABLE_GLSLANG_BINARIES OFF CACHE BOOL "" FORCE)
set(ENABLE_SPVREMAPPER OFF CACHE BOOL "" FORCE)
set(ENABLE_HLSL OFF CACHE BOOL "" FORCE)
set(ENABLE_CTEST OFF CACHE BOOL "" FORCE)
set(GLSLANG_TESTS OFF CACHE BOOL "" FORCE)
set(GLSLANG_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(BUILD_EXTERNAL OFF CACHE BOOL "" FORCE)
den_fetch(glslang https://github.com/KhronosGroup/glslang.git vulkan-sdk-1.3.296.0 GIT_SHALLOW ON)

set(SPIRV_CROSS_CLI OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_SHARED OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_STATIC ON CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_HLSL OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_MSL OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_CPP OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_REFLECT OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_C_API OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_UTIL OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_SKIP_INSTALL ON CACHE BOOL "" FORCE)
den_fetch(spirvcross https://github.com/KhronosGroup/SPIRV-Cross.git vulkan-sdk-1.3.296.0 GIT_SHALLOW ON)
