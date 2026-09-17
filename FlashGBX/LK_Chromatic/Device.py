import sys
from typing import cast, Tuple, Collection, Callable
from dataclasses import dataclass
import serial
import ctypes

from enum import Enum

from FlashGBX.Logging import dprint


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


    def __init__(
            self,
            port: str | None = None,
            baudrate: int = 9600,
            bytesize: int = 8,
            parity: str = "N",
            stopbits: float = 1,
            timeout: float | None = None,
            xonxoff: bool = False,
            rtscts: bool = False,
            write_timeout: float | None = None,
            dsrdtr: bool = False,
            inter_byte_timeout: float | None = None,
            exclusive: bool | None = None,
    ):
        super().__init__(port, baudrate, bytesize, parity, stopbits, timeout, xonxoff, rtscts, write_timeout, dsrdtr,
                         inter_byte_timeout, exclusive)
        self._papi = None

    def flush(self):
        self._papi.papi_send_to_lk_flush()
        pass

    def init_chromatic(self, papi):
        self._papi = papi

    def close(self):
        self._papi.papi_close()
        super().close()

    def read(self, size = 1) -> bytearray:
        buf = ctypes.create_string_buffer(size)
        self._papi.papi_recv_from_lk(buf, size)
        return bytearray(buf)

    def write(self, data):
        if not isinstance(data, (bytes, bytearray)):
            raise NotImplementedError()
        data = bytearray(data)
        count = len(data)
        buf_type = ctypes.c_char * count
        buf = buf_type.from_buffer(data)

        self._papi.papi_send_to_lk(ctypes.byref(buf), count)
        return len(data)

    @property
    def in_waiting(self):
        return self._papi.papi_recv_from_lk_pending_count()

    def reset_input_buffer(self):
        self._papi.papi_recv_from_lk_reset_input_buffer()

    def reset_output_buffer(self):
        self._papi.papi_send_to_lk_reset_output_buffer()
