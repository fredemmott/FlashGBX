add_library(
  openFPGAloader
  STATIC
  board.hpp
  cable.hpp
  jtagInterface.hpp
  part.hpp
  spiFlashdb.hpp

  portability/unistd.hpp

  configBitstreamParser.cpp
  configBitstreamParser.hpp
  device.cpp
  device.hpp
  fsparser.cpp
  fsparser.hpp
  gowin.cpp
  gowin.hpp
  gwu2x_jtag.cpp
  gwu2x_jtag.hpp
  jtag.cpp
  jtag.hpp
  libusb_ll.cpp
  libusb_ll.hpp
  rawParser.cpp
  rawParser.hpp
  spiFlash.cpp
  spiFlash.hpp
  spiInterface.cpp
  spiInterface.hpp

  display.hpp
  progressBar.hpp
)
target_compile_definitions(
  openFPGAloader
  PUBLIC
  ENABLE_GOWIN_GWU2X=1
  ENABLE_GOWIN_SUPPORT=1
  PRIVATE
  NOMINMAX
  WIN32_LEAN_AND_MEAN
)

set_target_properties(
  openFPGAloader
  PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF
)

target_link_libraries(
  openFPGAloader
  usb-1.0 # In future versions, this will be libusb::usb-1.0
)
