from . import commands
from .vars import *
from .Command import Command
from .Interface import Interface
from .State import State

import time
from typing import cast, Tuple, Collection
from dataclasses import dataclass
from queue import SimpleQueue
from collections import deque
from itertools import batched

import serial
import threading
import struct


# Mixin for USB serial device
# Converts LK_Device serial commands to Chromatic Microcode
#
# TODO:
# - explain microcode decision
# - composition over inheritance? more work, maybe less brittle
# - deques of bytes | bytearray
class Device(serial.Serial, Interface):
    @dataclass(frozen=True)
    class ShutdownSignal:
        pass

    _command: Command | None
    _lk_request_queue: SimpleQueue[bytes|ShutdownSignal]
    _lk_response_deque: deque[int|NotImplementedError]
    _lk_response_condition: threading.Condition

    _cart_queue : bytearray

    _vars: dict[int, int]
    _state: State

    _commands: dict[int, type[Command]]

    _executor_thread: threading.Thread

    @classmethod
    def _typed_subclasses(cls, root: type[Command]) -> list[type[Command]]:
        return root.__subclasses__()

    @classmethod
    def _all_commands(cls, root: type[Command]):
        klasses = set(cls._typed_subclasses(root))
        while True:
            before = len(klasses)
            for sub in list(klasses):
                klasses.update(cls._typed_subclasses(sub))
            if len(klasses) == before:
                return [klass for klass in klasses if not klass.__abstractmethods__]

    def init_chromatic(self):
        self._command = None
        self._lk_request_queue = SimpleQueue()
        self._lk_response_deque = deque()
        self._lk_response_condition = threading.Condition()

        self._cart_queue = bytearray()

        self._vars = {}
        self._state = State()

        self._commands = {}
        for klass in self._all_commands(Command):
            self._commands[klass.opcode()] = klass

        self._executor_thread = threading.Thread(target=self._executor_main, daemon=True, name="")
        self._executor_thread.start()

    def close(self):
        self._lk_request_queue.put(self.ShutdownSignal())
        self._executor_thread.join()
        super().close()

    def read(self, size = 1) -> bytearray:
        timeout = super().timeout
        timeout_at = (time.monotonic() + timeout) if timeout is not None else None
        ret = bytearray()
        while len(ret) < size:
            remaining = size - len(ret)
            with self._lk_response_condition:
                while not self._lk_response_deque:
                    if timeout_at is None:
                        self._lk_response_condition.wait()
                    else:
                        timeout = timeout_at - time.monotonic()
                        if timeout <= 0:
                            return ret
                        self._lk_response_condition.wait(timeout)
                        if not self._lk_response_deque:
                            return ret
                available  = len(self._lk_response_deque)
                to_read = min(remaining, available)
                items = [self._lk_response_deque.popleft() for _ in range(to_read)]
                for item in items:
                    if isinstance(item, Exception):
                        raise item
                    ret.append(item)
        return ret

    def write(self, data):
        if not isinstance(data, (bytes, bytearray)):
            raise NotImplementedError()
        self._lk_request_queue.put(bytes(data))
        return len(data)

    @property
    def in_waiting(self):
        with self._lk_response_condition:
            return len(self._lk_response_deque)

    def usb_write(self, data) -> int | None:
        count = super().write(data)
        assert(count is not None)
        assert(count == len(data))
        return count
    def usb_read(self, size) -> bytes:
        return super().read(size)
    def usb_read_byte(self):
        ret = self.usb_read(1)
        return ret[0] if ret else None

    @property
    def usb_in_waiting(self):
        return super().in_waiting

    def lk_response(self, data: bytes):
        with self._lk_response_condition:
            self._lk_response_deque.extend(data)
            self._lk_response_condition.notify()

    def mc_enqueue(
            self,
            reqs: Collection[Tuple[int, int]],
            flush: bool = True,
            is_write: bool = False,
            is_flash: bool = False,
            wait_for_status: bool = False) -> None:
        # Verilog:
        #
        #     ENQUEUE_RX_BYTE_0: begin
        #         req_o.address[15:8] <= rx_data_r;
        #         ENQUEUE_remaining <= ENQUEUE_remaining - 8'd1;
        #     end
        #     ENQUEUE_RX_BYTE_1: req_o.address[7:0] <= rx_data_r;
        #     ENQUEUE_RX_BYTE_2: req_o.data <= rx_data_r;
        #     ENQUEUE_RX_BYTE_3: begin
        #         enqueue_o <= 1'b1;
        #         req_o.is_write <= rx_data_r[0];
        #         req_o.is_flash <= rx_data_r[1];
        #         req_o.wait_for_status <= rx_data_r[2];
        #     end
        flags = (is_write << 0) | (is_flash << 1) | (wait_for_status << 2)
        layout = struct.Struct(">HBB")
        for address, data in reqs:
            self._cart_queue.extend(struct.pack(">HBB", address, data, flags))

        if flush:
            count = len(self._cart_queue) // 4
            if count > 0xFF:
                raise ValueError("Cannot enqueue more than 255 requests")
            buffer = bytearray([0x02, count])
            buffer.extend(self._cart_queue)
            self._cart_queue.clear()

            self.usb_write(buffer)
            self.mc_wait_for_ack()

    def mc_disable_cart(self):
        self.usb_write(b"\x00")
        self.mc_wait_for_ack()

    def mc_set_variables(self, fw_vars: dict[int, int]):
        self._vars = fw_vars
        # Verilog:
        #
        #     vars_o <= '{
        #         flash_we_pin: rx_data_r[1:0],
        #         hold_pin_audio: rx_data_r[2],
        #         dmg_read_cs_pulse: rx_data_r[3],
        #         dmg_write_cs_pulse: rx_data_r[4]
        #     };
        value = self._vars.get(VAR_IDX_FLASH_WE_PIN, 0) & 0b11
        value = value | (self._vars.get(VAR_IDX_HOLD_PIN_AUDIO, 0) << 2)
        value = value | (self._vars.get(lookup_var_key("DMG_READ_CS_PULSE"), 0) << 3)
        value = value | (self._vars.get(lookup_var_key("DMG_WRITE_CS_PULSE"), 0) << 4)

        buffer = struct.pack("BB", 0x01, value)
        self.usb_write(buffer)
        self.mc_wait_for_ack()

    def mc_poll(self, count: int) -> bytes:
        command = struct.pack("BB", 0x03, count)
        self.usb_write(command)
        ret = self.usb_read(count)
        if len(ret) != count:
            raise Exception(f"Expected {count} bytes, got {len(ret)} bytes")
        return ret

    def mc_exec(
            self,
            reqs: Collection[Tuple[int, int]],
            is_write: bool = False,
            is_flash: bool = False,
            wait_for_status: bool = False) -> bytes:
        self.mc_enqueue(reqs, flush=False, is_write=is_write, is_flash=is_flash, wait_for_status=wait_for_status)
        ret = self.mc_exec_enqueued()
        return ret

    # Returns (enqueued count, polled count)
    def mc_debug(self) -> (int, int):
        self.usb_write(b'\x05')
        return struct.unpack(">HH", self.usb_read(4))


    def mc_exec_enqueued(self) -> bytes:
        pending = 0
        ret = bytearray()
        max_chunk_commands = 0xFF
        bytes_per_command = 4
        max_chunk_bytes = max_chunk_commands * bytes_per_command
        total = 0
        for chunk in batched(self._cart_queue, max_chunk_bytes):
            assert(len(chunk) % bytes_per_command == 0)
            count = len(chunk) // bytes_per_command
            # Device FIFO has 512 items so we can do have two chunks in-flight
            if pending > max_chunk_commands:
                ret.extend(self.mc_poll(max_chunk_commands))
                pending -= max_chunk_commands

            buffer = bytearray([0x02, count])
            buffer.extend(chunk)

            self.usb_write(buffer)
            self.mc_wait_for_ack()
            pending += count
            total += count
        self._cart_queue.clear()
        while pending > 0:
            count = min(pending, 0xFF)
            ret.extend(self.mc_poll(count))
            pending -= count
        return ret

    def mc_ping(self, challenge: int) -> int:
        self.usb_write(struct.pack("BB", 0x04, challenge))
        response = self.usb_read_byte()
        if not response:
            raise Exception("No response to microcode ping")
        return response

    def mc_wait_for_ack(self):
        ret = self.usb_read_byte()
        if ret != 0x01:
            raise Exception("Expected ACK, device response was not 0x01")

    def _executor_main(self):
        while self.is_open:
            buf = self._lk_request_queue.get()
            if isinstance(buf, self.ShutdownSignal):
                return

            if self._command is not None and self._command.is_complete:
                self._command = None

            if self._command is not None:
                self._command.from_lk(buf)
                continue

            if buf[0] in self._commands:
                klass = self._commands[buf[0]]
                self._command = klass(self._vars.copy(), self._state, self)
            else:
                keys = [k for k, v in LK_Device.DEVICE_CMD.items() if v == buf[0]]
                if len(keys) == 1:
                    cmd = f"{keys[0]} (0x{buf[0]:02X})"
                else:
                    cmd = f"Unknown command (0x{buf[0]:02X})"
                with self._lk_response_condition:
                    self._lk_response_deque.append(NotImplementedError(cmd))
                    self._lk_response_condition.notify()
            if len(buf) > 1:
                cast(Command, self._command).from_lk(buf[1:])
