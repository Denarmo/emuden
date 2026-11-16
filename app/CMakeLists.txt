# The executable. Sources under src/ and screens/ are globbed: a feature adds files, and its
# DEN_REGISTER_* lines run because every object file of an executable is linked.
file(GLOB_RECURSE app_sources CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/screens/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/pages/*.cpp")
# Developer tools and the stand-ins built when they are left out (EMUDEN_DEV_TOOLS).
set(dev_tools_files automation fake_host sample_pages test_endpoint web_endpoint)
set(dev_tools_stubs automation_off fake_host_off)
if(EMUDEN_DEV_TOOLS)
    set(left_out ${dev_tools_stubs})
else()
    set(left_out ${dev_tools_files})
endif()
foreach(name ${left_out})
    list(FILTER app_sources EXCLUDE REGEX "/${name}[.]cpp$")
endforeach()

# What the screens decide without a window (interface looks, core option notes, the browser's
# rules), with its tests in app/tests.
file(GLOB app_logic_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/logic/*.cpp")
add_library(den_app_logic STATIC ${app_logic_sources})
target_include_directories(den_app_logic PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/logic")
target_link_libraries(den_app_logic PUBLIC den_plugins den_gfx den_platform den_patching den_base)
den_warnings(den_app_logic)
den_tests(app den_app_logic)

if(ANDROID)
    # SDL's activity loads libmain.so and calls SDL_main (shells/android). SDL is linked in,
    # so its JNI_OnLoad must be kept.
    add_library(den_app SHARED ${app_sources}
        "${CMAKE_SOURCE_DIR}/shells/android/app/src/main/cpp/entry.cpp")
    set_target_properties(den_app PROPERTIES OUTPUT_NAME main)
    target_link_options(den_app PRIVATE -Wl,-u,JNI_OnLoad)
else()
    add_executable(den_app ${app_sources})
    set_target_properties(den_app PROPERTIES OUTPUT_NAME emuden)
endif()
target_include_directories(den_app PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include" "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(den_app PRIVATE den_app_logic
    den_sync den_system den_achievements den_player den_engine den_plugins den_library den_store
    den_ui den_input den_gfx den_platform den_base den_qrcodegen)
den_warnings(den_app)
if(WIN32)
    # The icon Explorer and the taskbar show for emuden.exe.
    target_sources(den_app PRIVATE "${CMAKE_SOURCE_DIR}/shells/windows/emuden.rc")
endif()
if(MSVC)
    # No console window; main stays the entry point.
    target_link_options(den_app PRIVATE /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup)
endif()

# The fonts and other assets next to the executable, so a build folder runs as it is.
add_custom_command(TARGET den_app POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
        "${CMAKE_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:den_app>/assets")
