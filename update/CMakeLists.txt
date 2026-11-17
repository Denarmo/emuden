den_module(update DEPS den_base den_platform)
target_compile_definitions(den_update PRIVATE DEN_UPDATE_REPO="${EMUDEN_UPDATE_REPO}")
