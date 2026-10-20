add_library(SmpsAdapterBase STATIC SmpsAdapterBase.hxx SmpsAdapterBase.cxx)
add_library(vgmc::adapters::SmpsAdapterBase ALIAS SmpsAdapterBase)

target_link_libraries(SmpsAdapterBase PUBLIC
  vgmc::chips::YM2612
  vgmc::chips::SN76489
  vgmc::mixins::YM2612SnapshotLogger
  vgmc::mixins::SN76489SnapshotLogger
  Boost::container
  spdlog::spdlog
)
