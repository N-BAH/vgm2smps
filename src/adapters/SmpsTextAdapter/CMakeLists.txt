add_library(SmpsTextAdapter STATIC SmpsTextAdapter.hxx SmpsTextAdapter.cxx)
add_library(vgmc::adapters::SmpsTextAdapter ALIAS SmpsTextAdapter)
target_link_libraries(SmpsTextAdapter PRIVATE vgmc::adapters::SmpsAdapterBase)
