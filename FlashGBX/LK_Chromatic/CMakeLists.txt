add_library(
  _LK_Chromatic
  MODULE
  LK.c
  LK.h
  LK_device.cpp
  LK_device.h
  PAPI.hpp
)

set_target_properties(
  _LK_Chromatic
  PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF
  PREFIX "" # no `lib` prefix on Linux
)
target_compile_definitions(
  _LK_Chromatic
  PRIVATE
  WIN32_LEAN_AND_MEAN
  NOMINMAX
)

if (MSVC)
    # Enable conformant mode for the C preprocessor
    target_compile_options(_LK_Chromatic PRIVATE "/Zc:preprocessor")
endif()

install(TARGETS _LK_Chromatic DESTINATION FlashGBX)