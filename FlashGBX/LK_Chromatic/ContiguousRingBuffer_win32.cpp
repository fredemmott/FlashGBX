// Copyright 2026 Fred Emmott <fred@fredemmott.com>
// SPDX-License-Identifier: MIT

#include "ContiguousRingBuffer_win32.hpp"

#include "MC_impl_common.hpp"

#include <bit>
#include <numeric>

ContiguousRingBuffer::ContiguousRingBuffer(const std::size_t minimumSize) {
  SYSTEM_INFO info {};
  GetSystemInfo(&info);

  const auto addressSpaceSize = std::lcm<std::size_t>(
      static_cast<std::size_t>(info.dwAllocationGranularity) * 2,
      minimumSize * 2);
  _size = addressSpaceSize / 2;

  if (!std::has_single_bit(_size)) [[unlikely]] {
    LogError("ContiguousRingBuffer size {} is not a power of two", _size);
    abort();
  }

  // Get address space for the ring buffer...
  _buffer = static_cast<uint8_t*>(VirtualAlloc2(
      GetCurrentProcess(),
      nullptr,
      addressSpaceSize,
      MEM_RESERVE | MEM_RESERVE_PLACEHOLDER,
      PAGE_NOACCESS,
      nullptr,
      0));
  // ... but split it in two by 'freeing' the already-not-allocated second half
  VirtualFree(_buffer + _size, _size, MEM_RELEASE | MEM_PRESERVE_PLACEHOLDER);

  _file = CreateFileMapping(
    INVALID_HANDLE_VALUE,
    nullptr,
    PAGE_READWRITE,
    static_cast<DWORD>(_size >> 32), // ... not that we're ever going to send > 4GB to a GameBoy cartridge, but... might as well ¯\_(ツ)_/¯
    static_cast<DWORD>(_size & 0xFFFF'FFFF),
    nullptr);
  MapViewOfFile3(_file, GetCurrentProcess(), _buffer,         0, _size, MEM_REPLACE_PLACEHOLDER, PAGE_READWRITE, nullptr, 0);
  MapViewOfFile3(_file, GetCurrentProcess(), _buffer + _size, 0, _size, MEM_REPLACE_PLACEHOLDER, PAGE_READWRITE, nullptr, 0);
}

ContiguousRingBuffer::~ContiguousRingBuffer() {
  UnmapViewOfFile(_buffer + _size);
  UnmapViewOfFile(_buffer);
  CloseHandle(_file);
}