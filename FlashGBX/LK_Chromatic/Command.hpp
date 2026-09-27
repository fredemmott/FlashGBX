// Copyright 2026 Fred Emmott <fred@fredemmott.com>
// SPDX-License-Identifier: MIT
#pragma once

#include <cinttypes>

enum class Command : uint8_t {
  NOP = 0,
  Ping = 1,
  Delay = 2,
  Flush = 3,

  SetAddressMSB = 4,
  SetAddressLSB = 5,
  SetOutputEnable = 6,
  SetData = 7,
  GetData = 8,
  SetPinsA = 9,
  SetPinsB = 10,
  VerifyData = 11,
  VerifyStatusRegister = 12,
  SetStatusRegisterMask = 13,
  SetStatusRegisterValue = 14,
  GetStateBits = 15,
  GetFWInfo = 16,
};

