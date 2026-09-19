// Copyright 2026 Fred Emmott <fred@fredemmott.com>
// SPDX-License-Identifier: MIT
#pragma once

// A ring buffer using MMU tricks so that any valid region can be accessed
// contiguously

#ifdef _WIN32
#include "ContiguousRingBuffer_win32.hpp"
#elif __APPLE__
#include "ContiguousRingBuffer_mach.hpp"
#elif __linux__
#include "ContiguousRingBuffer_linux.hpp"
#else
#error "ContiguousRingBuffer is not implemented on this platform"
// TODO:
// - linux: mmap, munmap, memfd
#endif
