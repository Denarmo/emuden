# den_module(<name> DEPS ... PRIVATE_DEPS ... EXCLUDE <regex>...): static library den_<name>
# from <dir>/src, public headers in <dir>/include, and den_<name>_tests from <dir>/tests when
# that folder has sources. Sources are globbed, so adding a file needs no CMake edit.
function(den_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /wd4100 /wd4244 /wd4267 /wd4505 /utf-8 /permissive-)
        target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wno-unused-parameter)
    endif()
endfunction()

function(den_tests name)
    if(NOT DEN_BUILD_TESTS)
        return()
    endif()
    file(GLOB tests CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp")
    if(NOT tests)
        return()
    endif()
    add_executable(den_${name}_tests ${tests})
    target_link_libraries(den_${name}_tests PRIVATE ${ARGN} Catch2::Catch2WithMain)
    # Shared by the modules that read archives: 7z files a 7z tool wrote (base/tests/data)
    # and a writer of stored ones (base/tests/seven_zip_writer.h).
    target_include_directories(den_${name}_tests PRIVATE "${CMAKE_SOURCE_DIR}/base/tests")
    target_compile_definitions(den_${name}_tests PRIVATE
        DEN_ARCHIVE_FIXTURES="${CMAKE_SOURCE_DIR}/base/tests/data")
    den_warnings(den_${name}_tests)
    add_test(NAME den_${name}_tests COMMAND den_${name}_tests)
endfunction()

function(den_module name)
    cmake_parse_arguments(M "" "" "DEPS;PRIVATE_DEPS;EXCLUDE" ${ARGN})
    file(GLOB_RECURSE sources CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c")
    foreach(pattern ${M_EXCLUDE})
        list(FILTER sources EXCLUDE REGEX "${pattern}")
    endforeach()
    add_library(den_${name} STATIC ${sources})
    target_include_directories(den_${name} PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
        PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(den_${name} PUBLIC ${M_DEPS} PRIVATE ${M_PRIVATE_DEPS})
    den_warnings(den_${name})
    den_tests(${name} den_${name})
endfunction()
