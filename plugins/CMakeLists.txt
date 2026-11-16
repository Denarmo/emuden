den_module(plugins DEPS den_base den_platform den_store den_library den_patching PRIVATE_DEPS den_lua)

# Monocypher (Ed25519) is vendored as it is; its warnings are not ours.
file(GLOB monocypher "${CMAKE_CURRENT_SOURCE_DIR}/src/third_party/monocypher/*.c")
set_source_files_properties(${monocypher} PROPERTIES COMPILE_OPTIONS "-w")

# The official plugin manifests (assets/plugins/<id>/plugin.json) are compiled in, so the
# consoles are there before anything was downloaded.
file(GLOB bundled CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/assets/plugins/*/plugin.json")
list(SORT bundled)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${bundled})
set(code "// Generated from assets/plugins by plugins/CMakeLists.txt.\n#include \"den/plugins/manifest.h\"\n\nnamespace den::plugins {\nnamespace {\n")
set(table "")
set(count 0)
foreach(file ${bundled})
    get_filename_component(dir "${file}" DIRECTORY)
    get_filename_component(id "${dir}" NAME)
    file(READ "${file}" hex HEX)
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
    string(REGEX REPLACE "(0x[0-9a-f][0-9a-f],0x[0-9a-f][0-9a-f],0x[0-9a-f][0-9a-f],0x[0-9a-f][0-9a-f],0x[0-9a-f][0-9a-f],0x[0-9a-f][0-9a-f],0x[0-9a-f][0-9a-f],0x[0-9a-f][0-9a-f],0x[0-9a-f][0-9a-f],0x[0-9a-f][0-9a-f],0x[0-9a-f][0-9a-f],0x[0-9a-f][0-9a-f],0x[0-9a-f][0-9a-f],0x[0-9a-f][0-9a-f],0x[0-9a-f][0-9a-f],0x[0-9a-f][0-9a-f],)" "\\1\n" bytes "${bytes}")
    string(APPEND code "const unsigned char DATA_${count}[] = {\n${bytes}\n};\n")
    string(APPEND table "    {\"${id}\", DATA_${count}, sizeof(DATA_${count})},\n")
    math(EXPR count "${count} + 1")
endforeach()
string(APPEND code "const BundledManifest TABLE[] = {\n${table}    {nullptr, nullptr, 0},\n};\n} // namespace\n\nconst BundledManifest* BundledManifests(std::size_t& count) {\n    count = ${count};\n    return TABLE;\n}\n} // namespace den::plugins\n")
set(generated "${CMAKE_CURRENT_BINARY_DIR}/bundled_manifests.cpp")
file(CONFIGURE OUTPUT "${generated}" CONTENT "${code}" @ONLY)
target_sources(den_plugins PRIVATE "${generated}")

if(TARGET den_plugins_tests)
    target_compile_definitions(den_plugins_tests PRIVATE
        DEN_PLUGINS_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures"
        DEN_CONTRACTS_DIR="${CMAKE_SOURCE_DIR}/contracts")
    # The feature tests drive the script runner directly.
    target_include_directories(den_plugins_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(den_plugins_tests PRIVATE den_lua)
endif()

# Fails when a bundled manifest differs from the official one in an emuden-plugins checkout
# beside this one; tools/sync_bundled_plugins.py copies them. emuden.ps2 is left out until
# the PS2 plugin work in progress has landed in both repositories.
set(EMUDEN_PLUGINS_DIR "${CMAKE_SOURCE_DIR}/../emuden-plugins" CACHE PATH "An emuden-plugins checkout")
if(TARGET den_plugins_tests AND EXISTS "${EMUDEN_PLUGINS_DIR}/plugins")
    # Its signed manual packages are checked against the keys compiled in.
    target_compile_definitions(den_plugins_tests PRIVATE DEN_EMUDEN_PLUGINS_DIR="${EMUDEN_PLUGINS_DIR}")
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        add_test(NAME den_bundled_plugins_in_sync
            COMMAND Python3::Interpreter -I "${CMAKE_SOURCE_DIR}/tools/sync_bundled_plugins.py"
                --check --plugins "${EMUDEN_PLUGINS_DIR}" --skip emuden.ps2)
    endif()
endif()
