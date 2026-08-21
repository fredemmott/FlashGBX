include(FetchContent)

set(LIBUSB_INSTALL_TARGETS OFF)
FetchContent_Declare(libusb_cmake
  GIT_REPOSITORY https://github.com/libusb/libusb-cmake.git
  GIT_TAG        c8477c10ac2ac6b1718d4d498e102b9f18b776f5 # v1.0.30-0
)
FetchContent_MakeAvailable(libusb_cmake)

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
  CXX_STANDARD 23
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
target_link_libraries(
  _LK_Chromatic
  PRIVATE
  Synchronization.lib
  usb-1.0 # In future versions, this will be libusb::usb-1.0
)

if (MSVC)
    # Enable conformant mode for the C preprocessor
    target_compile_options(_LK_Chromatic PRIVATE "/Zc:preprocessor")
endif()

install(TARGETS _LK_Chromatic DESTINATION FlashGBX)