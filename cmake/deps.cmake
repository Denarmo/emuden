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

if(DEN_BUILD_TESTS)
    den_fetch(catch2 https://github.com/catchorg/Catch2.git v3.8.1 GIT_SHALLOW ON)
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
