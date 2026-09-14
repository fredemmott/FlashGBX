// Copyright 2026 Fred Emmott <fred@fredemmott.com>
// SPDX-License-Identifier: MIT

#include "ContiguousRingBuffer_mach.hpp"

#include "MC_impl_common.hpp"

#include <mach/mach.h>
#include <mach/mach_vm.h>

#include <bit>
#include <numeric>
#include <unistd.h>

ContiguousRingBuffer::ContiguousRingBuffer(const std::size_t minimumSize) {
    const auto task = mach_task_self();
    const auto pageSize = static_cast<std::size_t>(getpagesize());

    const auto addressSpaceSize = std::lcm<std::size_t>(pageSize * 2, minimumSize * 2);
    _size = addressSpaceSize / 2;

    mach_port_t memoryHandle = MACH_PORT_NULL;
    mach_vm_size_t handleSize = _size;
    mach_make_memory_entry_64(
        task,
        &handleSize,
        0,
        VM_PROT_READ | VM_PROT_WRITE | MAP_MEM_NAMED_CREATE,
        &memoryHandle,
        MACH_PORT_NULL);

    mach_vm_address_t address {};
    mach_vm_allocate(task, &address, addressSpaceSize, VM_FLAGS_ANYWHERE);

    _buffer = std::bit_cast<uint8_t*>(address);

    mach_vm_map(
        task,
        &address,
        _size,
        0,
        VM_FLAGS_FIXED | VM_FLAGS_OVERWRITE,
        memoryHandle,
        0,
        FALSE,
        VM_PROT_READ | VM_PROT_WRITE,
        VM_PROT_READ | VM_PROT_WRITE,
        VM_INHERIT_DEFAULT);

    address = std::bit_cast<mach_vm_address_t>(address) + _size;
    mach_vm_map(
        task,
        &address,
        _size,
        0,
        VM_FLAGS_FIXED | VM_FLAGS_OVERWRITE,
        memoryHandle,
        0,
        FALSE,
        VM_PROT_READ | VM_PROT_WRITE,
        VM_PROT_READ | VM_PROT_WRITE,
        VM_INHERIT_DEFAULT);

    // the mappings keep internal references
    mach_port_deallocate(task, memoryHandle);
}

ContiguousRingBuffer::~ContiguousRingBuffer() {
    const task_t task = mach_task_self();
    mach_vm_deallocate(task, std::bit_cast<mach_vm_address_t>(_buffer), _size * 2);
}