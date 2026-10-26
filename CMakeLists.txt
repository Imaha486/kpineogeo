# MSVC build (Visual Studio 2022)
#   cmake -S . -B build-x64 -A x64   && cmake --build build-x64 --config Release
#   cmake -S . -B build-x86 -A Win32 && cmake --build build-x86 --config Release
cmake_minimum_required(VERSION 3.20)
project(kpineogeo LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_library(kpineogeo SHARED
  src/kpineogeo.cpp src/kpi_sdk.cpp src/neoplayer.cpp src/songscan.cpp src/neosnd.cpp src/romfind.cpp src/neocrypt.cpp
  third_party/ymfm/ymfm_opn.cpp third_party/ymfm/ymfm_adpcm.cpp third_party/ymfm/ymfm_ssg.cpp
  third_party/z80/z80.c
  kpineogeo.def)
target_include_directories(kpineogeo PRIVATE src third_party/ymfm third_party/z80 third_party/kpisdk)
target_compile_definitions(kpineogeo PRIVATE UNICODE _UNICODE NOMINMAX _CRT_SECURE_NO_WARNINGS)
target_link_libraries(kpineogeo PRIVATE shell32)
if(MSVC)
  target_compile_options(kpineogeo PRIVATE /utf-8 /W3)
else()
  target_compile_options(kpineogeo PRIVATE -Wno-attributes)
  target_link_options(kpineogeo PRIVATE -static -Wl,--kill-at)
endif()
set_target_properties(kpineogeo PROPERTIES PREFIX "" SUFFIX ".kpi")
