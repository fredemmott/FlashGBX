// Copyright 2026 Fred Emmott <fred@fredemmott.com>
// SPDX-License-Identifier: MIT

#include "ContiguousRingBuffer_linux.hpp"

#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <bit>
#include <numeric>
#include <stdexcept>

ContiguousRingBuffer::ContiguousRingBuffer(const std::size_t minimumSize) {
    const auto pageSize = static_cast<std::size_t>(sysconf(_SC_PAGESIZE));

    _size = std::bit_ceil(std::max<std::size_t>(pageSize, minimumSize));
    const auto addressSpaceSize = _size * 2;

    const int fd = memfd_create("ContiguousRingBuffer", MFD_CLOEXEC);
    if (fd == -1) {
        throw std::runtime_error("Failed to create memfd");
    }

    if (ftruncate(fd, static_cast<off_t>(_size)) == -1) {
        close(fd);
        throw std::runtime_error("Failed to resize memfd");
    }

    // Reserve a contiguous region of virtual memory for both copies
    void* placeholder = mmap(
        nullptr,
        addressSpaceSize,
        PROT_NONE,
        MAP_PRIVATE | MAP_ANONYMOUS,
        -1,
        0);

    if (placeholder == MAP_FAILED) {
        close(fd);
        throw std::runtime_error("Failed to reserve virtual address space");
    }

    _buffer = static_cast<uint8_t*>(placeholder);

    // Map the first half to the backing memory
    void* firstHalf = mmap(
        _buffer,
        _size,
        PROT_READ | PROT_WRITE,
        MAP_SHARED | MAP_FIXED,
        fd,
        0);

    // Map the second half to the same backing memory
    void* secondHalf = mmap(
        _buffer + _size,
        _size,
        PROT_READ | PROT_WRITE,
        MAP_SHARED | MAP_FIXED,
        fd,
        0);

    // Close the file descriptor as the mappings retain their reference
    close(fd);

    if (firstHalf == MAP_FAILED || secondHalf == MAP_FAILED) {
        munmap(_buffer, addressSpaceSize);
        _buffer = nullptr;
        _size = 0;
        throw std::runtime_error("Failed to map contiguous ring buffer memory");
    }
}

ContiguousRingBuffer::~ContiguousRingBuffer() {
    if (_buffer) {
        munmap(_buffer, _size * 2);
    }
}
