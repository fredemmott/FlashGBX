// Copyright 2026 Fred Emmott <fred@fredemmott.com>
// SPDX-License-Identifier: MIT
#pragma once

#include "ContiguousRingBufferBase.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

#include <Windows.h>

struct ContiguousRingBuffer : ContiguousRingBufferBase {
  explicit ContiguousRingBuffer(std::size_t minimumSize);
  ~ContiguousRingBuffer();

private:
  HANDLE _file { INVALID_HANDLE_VALUE };
};