import sys
from typing import cast, Tuple, Collection, Callable
from dataclasses import dataclass
import serial
import ctypes

from enum import Enum

# For debug logging
class Command(int, Enum):
    CMD_PING = 0
    CMD_DELAY_MICROS = 1
    CMD_DELAY_NANOS = 2
    CMD_SET_PINS = 3
    CMD_SET_OUTPUT_ENABLE = 4
    CMD_SET_ADDRESS = 5
    CMD_SET_DATA = 6
    CMD_GET_DATA = 7

# Mixin for USB serial device
# Converts LK_Device serial commands to Chromatic Microcode
#
# TODO:
# - explain microcode decision
# - composition over inheritance? more work, maybe less brittle
# - deques of bytes | bytearray
class Device(serial.Serial):
    @dataclass(frozen=True)
    class ShutdownSignal:
        pass

    _write_from_flashgbx: Callable[[ctypes.POINTER(ctypes.c_uint8), ctypes.c_uint16], None]
    _read_to_flashgbx: Callable[[ctypes.POINTER(ctypes.c_uint8), ctypes.c_uint16], None]

    def flush(self):
        pass

    def lk_on_error(self, data: bytes) -> None:
        pass

    def init_chromatic(self, read_from_flashgbx, write_from_flashgbx):
        self._read_to_flashgbx = read_from_flashgbx
        self._write_from_flashgbx = write_from_flashgbx

    def close(self):
        super().close()

    def read(self, size = 1) -> bytearray:
        buf = ctypes.create_string_buffer(size)
        self._read_to_flashgbx(buf, size)
        return bytearray(buf)

    def write(self, data):
        if not isinstance(data, (bytes, bytearray)):
            raise NotImplementedError()
        data = bytearray(data)
        count = len(data)
        buf_type = ctypes.c_char * count
        buf = buf_type.from_buffer(data)

        self._write_from_flashgbx(ctypes.byref(buf), count)
        return len(data)

    @property
    def in_waiting(self):
        return 0

    def reset_input_buffer(self):
        pass

    def reset_output_buffer(self):
        pass
