# The executable. Sources under src/ and screens/ are globbed: a feature adds files, and its
# DEN_REGISTER_* lines run because every object file of an executable is linked.
file(GLOB_RECURSE app_sources CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/screens/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/pages/*.cpp")

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
target_link_libraries(den_app PRIVATE
    den_system den_player den_engine den_plugins den_library den_store
    den_ui den_input den_gfx den_platform den_base)
den_warnings(den_app)
if(MSVC)
    # No console window; main stays the entry point.
    target_link_options(den_app PRIVATE /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup)
endif()

# The fonts and other assets next to the executable, so a build folder runs as it is.
add_custom_command(TARGET den_app POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
        "${CMAKE_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:den_app>/assets")
