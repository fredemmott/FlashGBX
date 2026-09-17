// Copyright 2026 Fred Emmott <fred@fredemmott.com>
// SPDX-License-Identifier: MIT
#include "PAPI_fpga.hpp"

extern "C" {
#include "MC_transport.h"
}

#include "MC_impl_common.hpp"

#include "openFPGAloader/jtag.hpp"
#include "openFPGAloader/gowin.hpp"
#include "openFPGAloader/progressBar.hpp"

#include <format>

namespace {

template<std::invocable<Gowin&> T>
void fpga_invoke(T&& fn, const std::string& path) {
  const auto& cable = cable_list.at("gwu2x");
  jtag_pins_conf_t pins_config {};
  Jtag jtag {
    cable,
    &pins_config,
    /* args.device = */ {},
    /* args.ftdi_serial = */ {},
    /* args.freq = */ 6'000'000, // DEFAULT_FREQ from main.cpp
    /* args.verbose = */ 0,
    /* args.ip_adr = */ "127.0.0.1",
    /* args.port = */ 0,
  };
  Gowin fpga {
    &jtag,
    path,
    /* args.file_type = */ {},
    /* args.mcufw = */ {},
    Device::prog_type_t::WR_SRAM,
    /* args.external_flash = */ false,
    /* args.verify = */ false,
    /* args.verbose = */ 0,
    /* args.user_flash = */ {},
  };
  std::invoke(std::forward<T>(fn), fpga);
}

PAPIStringCallback gMessageCallback { nullptr };

template<class... Args>
void message(
    const std::format_string<Args...>& fmt,
    Args&&... args) {
  if (!gMessageCallback) return;
  const auto msg = std::vformat(fmt.get(), std::make_format_args(args...));
  gMessageCallback(msg.data(), static_cast<uint16_t>(msg.size()));
};

void op_message(const std::string_view op) {
  message("Setting up Chromatic: {}...", op);
}

PAPIProgressCallback gProgressCallback { nullptr };
std::size_t gProgressMax {};
void update_progress(const std::size_t value) {
  if (!gProgressCallback) {
    return;
  }
  gProgressCallback(value, gProgressMax);
}

}

extern "C" int papi_fpga_program_sram(
  const char* const path,
  const size_t path_len,
  const PAPIStringCallback message_callback,
  const PAPIProgressCallback progress_callback)
try {
  dprint("papi_fpga_program_sram()");

  gMessageCallback = message_callback;
  gProgressCallback = progress_callback;
  const struct CallbackGuard {
    ~CallbackGuard() {
      gMessageCallback = nullptr;
      gProgressCallback = nullptr;
    }
  } callbackGuard;

  op_message("Connecting");

  fpga_invoke(
    [=](Gowin& fpga) {
      fpga.program(/* offset = */ 0, /* unprotect_flash = */ false);
    },
    {path, path_len}
  );

  op_message("Rebooting");
  dprint("papi_fpga_program_sram() complete");
  return 1;
} catch (const std::exception& e) {
  LogError("uncaught exception in papi_fpga_program_sram(): {}", e.what());
  return 0;
}

int papi_fpga_reset() try {
  fpga_invoke(&Gowin::reset, {});
  return 1;
} catch (const std::exception& e) {
  LogError("uncaught exception in papi_fpga_reset(): {}", e.what());
  return 0;
}

// openFPGAloader stubs

void printError(const std::string &err, bool eol) {
  LogError("openFPGAloader ERROR: {}", err);
}
void printWarn(const std::string &warn, bool eol) {
  LogError("openFPGAloader WARNING: {}", warn);
}
void printInfo(const std::string &info, bool eol) {
}
void printSuccess(const std::string &success, bool eol) {
}

ProgressBar::ProgressBar(const std::string &mess, int maxValue, int progressLen,
                         bool quiet) {
  op_message(mess);
  gProgressMax = maxValue;
  update_progress(0);
}
void ProgressBar::display(int value, char force) {
  update_progress(value);
}
void ProgressBar::done() {
  update_progress(std::exchange(gProgressMax, 0));
}
void ProgressBar::fail() {
  done(); // result code of operation is used
}
