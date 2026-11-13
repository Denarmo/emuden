den_module(player DEPS den_base den_platform den_gfx den_input den_store
    PRIVATE_DEPS SDL3::SDL3-static)
target_include_directories(den_player PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libretro")

# The tests run a tiny libretro core built here (tests/testcore).
if(TARGET den_player_tests)
    add_library(den_player_testcore MODULE tests/testcore/testcore.cpp)
    target_include_directories(den_player_testcore PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libretro")
    set_target_properties(den_player_testcore PROPERTIES PREFIX "" C_VISIBILITY_PRESET default
        CXX_VISIBILITY_PRESET default)
    target_include_directories(den_player_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(den_player_tests PRIVATE SDL3::SDL3-static)
    add_dependencies(den_player_tests den_player_testcore)
    target_compile_definitions(den_player_tests PRIVATE
        DEN_TESTCORE_PATH="$<TARGET_FILE:den_player_testcore>"
        DEN_SHADER_DIR="${CMAKE_SOURCE_DIR}/assets/shaders")
endif()

# A window that plays one game, to try the player without the app (tools/play.cpp).
add_executable(den_player_play EXCLUDE_FROM_ALL tools/play.cpp)
target_link_libraries(den_player_play PRIVATE den_player den_store den_input den_gfx den_platform den_base)
den_warnings(den_player_play)

# A still picture through a picture preset at any display size (tools/picture_shot.cpp).
add_executable(den_picture_shot EXCLUDE_FROM_ALL tools/picture_shot.cpp)
target_include_directories(den_picture_shot PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(den_picture_shot PRIVATE den_player den_gfx den_platform den_base)
den_warnings(den_picture_shot)
