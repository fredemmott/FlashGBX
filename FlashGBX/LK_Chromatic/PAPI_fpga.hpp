// Copyright 2026 Fred Emmott <fred@fredemmott.com>
// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>

#include "PAPI.hpp"

extern "C" {

using PAPIProgressCallback = void(*)(size_t value, size_t max);

// 1 on success, 0 on failure
// details are via mc_on_error
LK_CHROMATIC_EXPORT int papi_fpga_program_sram(
  const char* path,
  size_t path_len,
  PAPIStringCallback message_callback,
  PAPIProgressCallback progress_callback);
// 1 on success, 0 on failure
// details are via mc_on_error
LK_CHROMATIC_EXPORT int papi_fpga_reset();

}