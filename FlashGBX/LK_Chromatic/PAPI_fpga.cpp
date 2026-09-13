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

template<std::invocable<Gowin&> T>
static void fpga_invoke(T&& fn, const std::string& path) {
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

extern "C" int papi_fpga_program_sram(const char* const path, const size_t path_len) try {
  fpga_invoke(
    [=](Gowin& fpga) {
      fpga.program(/* offset = */ 0, /* unprotect_flash = */ false);
    },
    {path, path_len}
  );
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
  mc_on_error(err.data(), err.size());
}
void printWarn(const std::string &warn, bool eol) {
  mc_on_debug_message(warn.data(), warn.size());
}
void printInfo(const std::string &info, bool eol) {
  dprint("printInfo: {}", info);
}
void printSuccess(const std::string &success, bool eol) {
  dprint("printSuccess: {}", success);
}

ProgressBar::ProgressBar(const std::string &mess, int maxValue, int progressLen,
                         bool quiet) {
  dprint("ProgressBar::ProgressBar({}, {}, {}, {})", mess, maxValue, progressLen, quiet);
}
void ProgressBar::display(int value, char force) {
  dprint("ProgressBar::display({}, {})", value, static_cast<int>(force));
}
void ProgressBar::done() {
  dprint("ProgressBar::done()");
}
void ProgressBar::fail() {
  dprint("ProgressBar::fail()");
}
