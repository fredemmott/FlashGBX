// Copyright 2026 Fred Emmott <fred@fredemmott.com>
// SPDX-License-Identifier: MIT

#include "ContiguousRingBuffer_win32.hpp"

#include "MC_impl_common.hpp"

#include <bit>
#include <numeric>

ContiguousRingBuffer::ContiguousRingBuffer(const std::size_t minimumSize) {
  SYSTEM_INFO info {};
  GetSystemInfo(&info);

  _size = std::bit_ceil(std::max<std::size_t>(info.dwAllocationGranularity, minimumSize));
  const auto addressSpaceSize = _size * 2;

  // Get address space for the ring buffer...
  _buffer = static_cast<uint8_t*>(VirtualAlloc2(
      GetCurrentProcess(),
      nullptr,
      addressSpaceSize,
      MEM_RESERVE | MEM_RESERVE_PLACEHOLDER,
      PAGE_NOACCESS,
      nullptr,
      0));
  if (!_buffer) {
    _size = 0;
    LogError(
      "VirtualAlloc2() failed for {:#010x}-byte placeholder: {:#010x}; {} bytes requested with allocation granularity of {}",
      _size,
      std::bit_cast<uint32_t>(HRESULT_FROM_WIN32(GetLastError())),
      minimumSize,
      info.dwAllocationGranularity);
    return;
  }
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