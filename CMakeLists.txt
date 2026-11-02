cmake_minimum_required(VERSION 3.18.0)

project(vgm2smps LANGUAGES CXX VERSION 0.9.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(ZLIB REQUIRED)
find_package(Boost CONFIG REQUIRED COMPONENTS container program_options)
find_package(spdlog CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)

if("${CMAKE_VERSION}" VERSION_LESS "3.20")
  include(TestBigEndian)
  test_big_endian(ARCH_BIG_ENDIAN)
  if(ARCH_BIG_ENDIAN)
    set(CMAKE_C_BYTE_ORDER "BIG_ENDIAN")
  else()
    set(CMAKE_C_BYTE_ORDER "LITTLE_ENDIAN")
  endif()
endif()

# add defines for Little/Big Endian
if("${CMAKE_C_BYTE_ORDER}" STREQUAL "BIG_ENDIAN")
  add_definitions("-D VGMC_BIG_ENDIAN")
else()
  add_definitions("-D VGMC_LITTLE_ENDIAN")
endif()

add_library(global_dependencies INTERFACE)

set(PROJECT_EXE "${PROJECT_NAME}")

if(MSVC)
  #  Force UTF-8 in MSVC
  add_compile_options("/utf-8")
endif()

include_directories("src")

add_subdirectory(src/utils)
add_subdirectory(src/chips)
add_subdirectory(src/adapters)
add_subdirectory(src/mixins)
add_subdirectory(src/vgm_parser)

target_link_libraries(global_dependencies INTERFACE
  ZLIB::ZLIB
  fmt::fmt
  Boost::container
  Boost::program_options
  spdlog::spdlog $<$<BOOL:${MINGW}>:ws2_32>
)

add_executable(${PROJECT_EXE}
  src/main.cxx
  src/version.hxx
)

find_program(HAVE_GIT git)

if(HAVE_GIT)
  execute_process(
    COMMAND git --git-dir=.git log -1 --pretty=format:%h
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_COMMIT_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
  )
  execute_process(
    COMMAND git --git-dir=.git log -1 --pretty=format:%cd --date=format:%Y.%m.%d
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_COMMIT_DATE
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
  )

  if(GIT_COMMIT_HASH)
    add_compile_definitions(${PROJECT_EXE} PUBLIC
      VGMC_GIT_COMMIT_HASH="${GIT_COMMIT_HASH}"
    )
  endif()

  if(GIT_COMMIT_DATE)
    add_compile_definitions(${PROJECT_EXE} PUBLIC
      VGMC_GIT_COMMIT_DATE="${GIT_COMMIT_DATE}"
    )
  endif()
endif()

add_compile_definitions(${PROJECT_EXE} PUBLIC VGMC_VERSION="${PROJECT_VERSION}")

target_link_libraries(${PROJECT_EXE} PUBLIC global_dependencies
  # Object groups as STATIC libraries
  vgmc::Utils
  vgmc::Chips
  vgmc::OutputAdapters
  vgmc::VgmParser
)
