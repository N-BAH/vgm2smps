add_subdirectory(YM2612)
add_subdirectory(SN76489)

add_library(Chips INTERFACE)
add_library(vgmc::Chips ALIAS Chips)

target_link_libraries(Chips INTERFACE vgmc::chips::SN76489 vgmc::chips::YM2612)
