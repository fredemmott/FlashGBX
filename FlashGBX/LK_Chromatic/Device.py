import sys
import time
from typing import cast, Tuple, Collection, Callable
from dataclasses import dataclass
from queue import SimpleQueue
from collections import deque
from itertools import batched
from enum import Enum

import serial
import threading

from serial import serialwin32

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

    _from_flashgbx_buf: bytes
    _from_flashgbx_queue: SimpleQueue[bytes|ShutdownSignal]
    _to_flashgbx_queue: deque[bytes]
    _to_flashgbx_condition: threading.Condition
    _lk_entrypoint: Callable[[int], None]

    _executor_thread: threading.Thread

    def native_handle(self):
        if sys.platform == "win32":
            return self._port_handle
        return None

    def lk_send_to_flashgbx(self, data: bytes) -> None:
        with self._to_flashgbx_condition:
            self._to_flashgbx_queue.append(data)
            self._to_flashgbx_condition.notify()

    def lk_recv_from_flashgbx(self, count: int) -> bytes:
        while count > len(self._from_flashgbx_buf):
            chunk = self._from_flashgbx_queue.get()
            if isinstance(chunk, bytes):
                self._from_flashgbx_buf += chunk
        ret = bytes(self._from_flashgbx_buf[:count])
        self._from_flashgbx_buf = self._from_flashgbx_buf[count:]
        return ret

    def lk_on_error(self, data: bytes) -> None:
        pass

    def init_chromatic(self, lk_entrypoint):
        self._lk_entrypoint = lk_entrypoint
        self._from_flashgbx_queue = SimpleQueue()
        self._from_flashgbx_buf = bytearray()
        self._to_flashgbx_queue = deque()
        self._to_flashgbx_condition = threading.Condition()

        self._executor_thread = threading.Thread(target=self._executor_main, daemon=True, name="")
        self._executor_thread.start()

    def close(self):
        self._from_flashgbx_queue.put(self.ShutdownSignal())
        self._executor_thread.join()
        super().close()

    def read(self, size = 1) -> bytearray:
        timeout = super().timeout
        timeout_at = (time.monotonic() + timeout) if timeout is not None else None
        ret = bytearray()
        while len(ret) < size:
            remaining = size - len(ret)
            with self._to_flashgbx_condition:
                while not self._to_flashgbx_queue:
                    if timeout_at is None:
                        self._to_flashgbx_condition.wait()
                    else:
                        timeout = timeout_at - time.monotonic()
                        if timeout <= 0:
                            return ret
                        self._to_flashgbx_condition.wait(timeout)
                        if not self._to_flashgbx_queue:
                            return ret
                available  = len(self._to_flashgbx_queue)
                to_read = min(remaining, available)
                items = [self._to_flashgbx_queue.popleft() for _ in range(to_read)]
                for item in items:
                    if isinstance(item, Exception):
                        raise item
                    ret.extend(item)
        return ret

    def write(self, data):
        if not isinstance(data, (bytes, bytearray)):
            raise NotImplementedError()
        self._from_flashgbx_queue.put(bytes(data))
        return len(data)

    @property
    def in_waiting(self):
        with self._to_flashgbx_condition:
            return len(self._to_flashgbx_queue)

    def _usb_write(self, data) -> int | None:
        count = super().write(data)
        assert(count is not None)
        assert(count == len(data))
        return count
    def _usb_read(self, size) -> bytes:
        return super().read(size)

    @property
    def _usb_in_waiting(self):
        return super().in_waiting

    def _executor_main(self):
        while self.is_open:
            buf = self._from_flashgbx_queue.get()
            if isinstance(buf, self.ShutdownSignal):
                return

            self._from_flashgbx_buf = buf[1:]
            self._lk_entrypoint(buf[0])

    def reset_input_buffer(self):
        pass

    def reset_output_buffer(self):
        pass
