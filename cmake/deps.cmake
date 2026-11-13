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
