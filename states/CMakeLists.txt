den_module(states DEPS den_base)

# Converts one state file, to try the converter on real states (tools/gba_state.cpp).
add_executable(den_gba_state EXCLUDE_FROM_ALL tools/gba_state.cpp)
target_link_libraries(den_gba_state PRIVATE den_states den_base)
den_warnings(den_gba_state)

if(TARGET den_states_tests)
    target_include_directories(den_states_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
endif()
