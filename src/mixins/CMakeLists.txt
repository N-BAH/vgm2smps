add_subdirectory(YM2612SnapshotLogger)
add_subdirectory(SN76489SnapshotLogger)

add_library(vgmc_mixins INTERFACE)
add_library(vgmc::mixins ALIAS vgmc_mixins)

target_link_libraries(vgmc_mixins INTERFACE vgmc::mixins::SN76489SnapshotLogger vgmc::mixins::YM2612SnapshotLogger)
