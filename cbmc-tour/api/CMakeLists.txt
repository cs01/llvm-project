# Build the libcprover-cpp example against a source build of CBMC.
#
#   git clone https://github.com/diffblue/cbmc ~/cbmc
#   cmake -S ~/cbmc -B ~/cbmc/build && cmake --build ~/cbmc/build -j
#
#   cmake -S . -B build -DCBMC_SOURCE_DIR=~/cbmc -DCBMC_BUILD_DIR=~/cbmc/build
#   cmake --build build
#
cmake_minimum_required(VERSION 3.16)
project(cbmc_api_example CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CBMC_SOURCE_DIR "" CACHE PATH "Root of a CBMC checkout")
set(CBMC_BUILD_DIR  "" CACHE PATH "CBMC CMake build directory")

if(NOT CBMC_SOURCE_DIR OR NOT CBMC_BUILD_DIR)
  message(FATAL_ERROR
    "Set -DCBMC_SOURCE_DIR=<checkout> -DCBMC_BUILD_DIR=<build dir>")
endif()

add_executable(cbmc_api_example example.cpp)

# api.h is included as <cprover/api.h>; CBMC's build stages it under
# src/libcprover-cpp. Adding both roots covers either layout.
target_include_directories(cbmc_api_example PRIVATE
  "${CBMC_SOURCE_DIR}/src"
  "${CBMC_SOURCE_DIR}/src/libcprover-cpp"
  "${CBMC_BUILD_DIR}/src")

target_link_directories(cbmc_api_example PRIVATE
  "${CBMC_BUILD_DIR}/lib"
  "${CBMC_BUILD_DIR}/src/libcprover-cpp")

# cprover-api-cpp is the facade; it pulls in the engine libraries below it.
# Order matters for static linking; --start-group avoids hand-sorting them.
target_link_libraries(cbmc_api_example PRIVATE
  -Wl,--start-group
  cprover-api-cpp
  goto-checker
  goto-instrument-lib
  goto-programs
  goto-symex
  pointer-analysis
  analyses
  solvers
  langapi
  ansi-c
  linking
  assembler
  big-int
  util
  json
  xml
  -Wl,--end-group)
