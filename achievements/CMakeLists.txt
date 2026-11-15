den_module(achievements DEPS den_base den_platform PRIVATE_DEPS den_rcheevos SDL3::SDL3-static)

# The hash and game id of one ROM, without signing in (tools/probe.cpp).
add_executable(den_achievements_probe EXCLUDE_FROM_ALL tools/probe.cpp)
target_link_libraries(den_achievements_probe PRIVATE den_achievements den_platform den_base)
den_warnings(den_achievements_probe)
