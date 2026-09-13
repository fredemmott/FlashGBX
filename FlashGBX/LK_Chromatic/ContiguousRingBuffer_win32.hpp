// Copyright 2026 Fred Emmott <fred@fredemmott.com>
// SPDX-License-Identifier: MIT
#pragma once

#include <Windows.h>
#include <cstddef>
#include <cstdint>
#include <span>

struct ContiguousRingBuffer {
  explicit ContiguousRingBuffer(std::size_t minimumSize);
  ~ContiguousRingBuffer();

  [[nodiscard]]
  uint8_t* data() noexcept {
    return _buffer;
  }

  [[nodiscard]]
  const uint8_t* data() const noexcept {
    return _buffer;
  }

  [[nodiscard]]
  std::size_t size() const noexcept {
    return _size;
  }

  [[nodiscard]]
  std::size_t virtual_size() const noexcept {
    return _size * 2;
  }

  [[nodiscard]]
  std::span<uint8_t> subspan(const std::size_t offset, const std::size_t count) noexcept {
    return {
      // _size is guaranteed to be a power of two, so (_size - 1) is the valid
      // bits, effectively giving us `offset % _size`
      _buffer + (offset & (_size - 1)),
      count,
    };
  }

  [[nodiscard]]
  std::span<const uint8_t> subspan(const std::size_t offset, const std::size_t count) const noexcept {
    return {
      // _size is guaranteed to be a power of two, so (_size - 1) is the valid
      // bits, effectively giving us `offset % _size`
      _buffer + (offset & (_size - 1)),
      count,
    };
  }
private:
  uint8_t* _buffer {};
  std::size_t _size {};

  HANDLE _file { INVALID_HANDLE_VALUE };
public:
  ContiguousRingBuffer() = delete;
  ContiguousRingBuffer(const ContiguousRingBuffer&) = delete;
  ContiguousRingBuffer(ContiguousRingBuffer&&) = delete;
  ContiguousRingBuffer& operator=(const ContiguousRingBuffer&) = delete;
  ContiguousRingBuffer& operator=(ContiguousRingBuffer&&) = delete;
};