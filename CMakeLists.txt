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

include(cmake/deps.cmake)
include(cmake/module.cmake)
if(DEN_BUILD_TESTS)
    enable_testing()
endif()

# Order matters: a directory never includes one listed after it.
foreach(dir base platform net patching gfx input ui store library plugins online engine player achievements system sync app)
    if(EXISTS "${CMAKE_SOURCE_DIR}/${dir}/CMakeLists.txt")
        add_subdirectory(${dir})
    endif()
endforeach()
