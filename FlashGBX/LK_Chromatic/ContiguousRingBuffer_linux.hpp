// Copyright 2026 Fred Emmott <fred@fredemmott.com>
// SPDX-License-Identifier: MIT
#pragma once

#include "ContiguousRingBufferBase.hpp"

struct ContiguousRingBuffer : ContiguousRingBufferBase {
    explicit ContiguousRingBuffer(std::size_t minimumSize);
    ~ContiguousRingBuffer();
};
