cmake_minimum_required(VERSION 2.8.8)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

project(FASTD C ASM)
set(FASTD_VERSION "v18+")

include(arch)
include(config)
include(deps)
include(checks)

set(DOXYFILE_SOURCE_DIR ${FASTD_SOURCE_DIR}/src)
include(UseDoxygen OPTIONAL)

add_subdirectory(src)
