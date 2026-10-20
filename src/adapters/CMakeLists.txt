add_subdirectory(SmpsAdapterBase)
add_subdirectory(SmpsTextAdapter)
# TODO: add more output adapters

add_library(OutputAdapters INTERFACE)
add_library(vgmc::OutputAdapters ALIAS OutputAdapters)

target_link_libraries(OutputAdapters INTERFACE vgmc::adapters::SmpsAdapterBase vgmc::adapters::SmpsTextAdapter)
