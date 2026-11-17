set(platform_exclude)
if(WIN32)
    list(APPEND platform_exclude "/src/posix/")
else()
    list(APPEND platform_exclude "/src/win/")
endif()
if(ANDROID)
    # HTTP comes from the platform through JNI (src/android).
    list(APPEND platform_exclude "/src/posix/http_curl.cpp")
    # Secrets are a Keystore-encrypted file of the shell (src/android).
    list(APPEND platform_exclude "/src/posix/secrets_file.cpp")
    # The second screen is an activity on the other display (src/android), not a second window.
    list(APPEND platform_exclude "/src/sdl/second_display_sdl.cpp")
    # Other apps and the system keyboard exist on Android only (src/android).
    list(APPEND platform_exclude "/src/sdl/apps_desktop.cpp" "/src/sdl/text_input_desktop.cpp")
    # Engines are apps of their own there (src/android).
    list(APPEND platform_exclude "/src/sdl/engine_apps_desktop.cpp")
    # The app goes on while its activity is stopped, and Android has a media volume.
    list(APPEND platform_exclude "/src/sdl/lifecycle_desktop.cpp")
    # Refresh rates are the window's preferred display mode (src/android).
    list(APPEND platform_exclude "/src/sdl/refresh_desktop.cpp")
    # The web view is android.webkit.WebView (src/android).
    list(APPEND platform_exclude "/src/posix/webview_posix.cpp")
    # Updates go to the package installer (src/android).
    list(APPEND platform_exclude "/src/posix/self_update_posix.cpp")
else()
    list(APPEND platform_exclude "/src/android/")
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT ANDROID)
    # The web view is WebKitGTK found at run time (src/linux); the stub is for the others.
    list(APPEND platform_exclude "/src/posix/webview_posix.cpp")
else()
    list(APPEND platform_exclude "/src/linux/")
endif()

den_module(platform DEPS den_base PRIVATE_DEPS SDL3::SDL3-static EXCLUDE ${platform_exclude})

if(WIN32)
    target_link_libraries(den_platform PRIVATE winhttp advapi32 ws2_32 iphlpapi dwmapi den_webview2)
elseif(ANDROID)
    target_link_libraries(den_platform PRIVATE EGL android)
else()
    find_package(CURL QUIET)
    if(CURL_FOUND)
        target_link_libraries(den_platform PRIVATE CURL::libcurl)
        target_compile_definitions(den_platform PRIVATE DEN_HAVE_CURL)
    endif()
    # dlopen for the web engine (src/linux): never a link dependency on it.
    target_link_libraries(den_platform PRIVATE ${CMAKE_DL_LIBS})
endif()
