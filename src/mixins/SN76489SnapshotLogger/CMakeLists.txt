add_library(vgmc_SN76489SnapshotLogger INTERFACE SN76489SnapshotLogger.hxx)
add_library(vgmc::mixins::SN76489SnapshotLogger ALIAS vgmc_SN76489SnapshotLogger)

# Header-Only mixin
target_include_directories(vgmc_SN76489SnapshotLogger INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)

target_link_libraries(vgmc_SN76489SnapshotLogger INTERFACE vgmc::chips::SN76489 vgmc::Utils)
