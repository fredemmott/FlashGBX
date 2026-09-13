// Copyright 2026 Fred Emmott <fred@fredemmott.com>
// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>

#define LK_CHROMATIC_EXPORT __declspec(dllexport)

extern "C" {

// 1 on success, 0 on failure
// details are via mc_on_error
LK_CHROMATIC_EXPORT int papi_fpga_program_sram(const char* path, size_t path_len);
// 1 on success, 0 on failure
// details are via mc_on_error
LK_CHROMATIC_EXPORT int papi_fpga_reset();

}