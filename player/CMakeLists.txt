den_module(player DEPS den_base den_platform den_gfx den_input den_store)
target_include_directories(den_player PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libretro")
