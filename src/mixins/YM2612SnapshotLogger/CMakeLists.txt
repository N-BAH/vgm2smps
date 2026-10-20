add_library(vgmc_YM2612SnapshotLogger INTERFACE YM2612SnapshotLogger.hxx)
add_library(vgmc::mixins::YM2612SnapshotLogger ALIAS vgmc_YM2612SnapshotLogger)

# Header-Only mixin
target_include_directories(vgmc_YM2612SnapshotLogger INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)

target_link_libraries(vgmc_YM2612SnapshotLogger INTERFACE
  vgmc::chips::YM2612
  vgmc::Utils
  spdlog::spdlog
)
