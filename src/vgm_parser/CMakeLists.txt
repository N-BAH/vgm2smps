add_library(vgmc_VgmParser OBJECT VgmParser.hxx VgmParser.cxx)
add_library(vgmc::VgmParser ALIAS vgmc_VgmParser)

target_link_libraries(vgmc_VgmParser PRIVATE vgmc::Chips vgmc::OutputAdapters spdlog::spdlog)
