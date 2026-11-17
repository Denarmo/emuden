cmake_minimum_required(VERSION 3.24)
project(emuden VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

option(DEN_BUILD_TESTS "Build the unit tests" ON)
# The automation and test endpoints and the sample plugin host (each still needs its environment
# variable at run time). Off leaves them out of the app.
option(EMUDEN_DEV_TOOLS "Build the developer and test tools into the app" ON)
# The release this build is ("0.2.0"), set by the packaging scripts. Empty is a development
# build, which never offers an update.
set(EMUDEN_VERSION "" CACHE STRING "Release version built into the app; empty for a development build")
# The GitHub repository ("owner/name") whose releases the app updater offers, for a public
# releases repository beside a private source one.
set(EMUDEN_UPDATE_REPO "Denarmo/emuden" CACHE STRING "GitHub owner/name the app takes its updates from")
if(NOT EMUDEN_UPDATE_REPO MATCHES "^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$")
    message(FATAL_ERROR "EMUDEN_UPDATE_REPO must be owner/name, not \"${EMUDEN_UPDATE_REPO}\"")
endif()

include(cmake/deps.cmake)
include(cmake/module.cmake)
if(DEN_BUILD_TESTS)
    enable_testing()
endif()

# Order matters: a directory never includes one listed after it.
foreach(dir base platform net patching states gfx input ui store library plugins online engine player achievements system update sync app)
    if(EXISTS "${CMAKE_SOURCE_DIR}/${dir}/CMakeLists.txt")
        add_subdirectory(${dir})
    endif()
endforeach()
