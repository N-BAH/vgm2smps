add_library(vgmc_YM2612 STATIC YM2612.hxx YM2612.cxx)
add_library(vgmc::chips::YM2612 ALIAS vgmc_YM2612)
target_link_libraries(vgmc_YM2612 PRIVATE spdlog::spdlog)
