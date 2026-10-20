add_library(vgmc_SN76489 STATIC SN76489.hxx SN76489.cxx)
add_library(vgmc::chips::SN76489 ALIAS vgmc_SN76489)
