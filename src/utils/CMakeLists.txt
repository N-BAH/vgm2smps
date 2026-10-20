find_package(ZLIB)

add_library(vgmc_Utils OBJECT
  readBinary.hxx VgmDecompressor.hxx
  VgmDecompressor.cxx
  NoteUtils.hxx NoteUtils.cxx
  getTerminalWidth.hxx getTerminalWidth.cxx
)

add_library(vgmc::Utils ALIAS vgmc_Utils)

target_link_libraries(vgmc_Utils PRIVATE ZLIB::ZLIB)
