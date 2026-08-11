from ..Command import Command
from ..Interface import Interface
from ..State import State

import struct

class SetFlashCmd(Command):
    command = "SET_FLASH_CMD"
    def __init__(self, fw_vars: dict[int, int], state: State, output: Interface):
        super().__init__(fw_vars, state, output)
        self._is_complete = False
        self._rx = bytearray()

    @property
    def is_complete(self) -> bool:
        return self._is_complete

    def from_lk(self, rx_data: bytes):
        # byte [0] command set (unused, AMD only for now)
        #      [1] flash method (unused, single byte only for now)
        #      [2] FLASH_WE_PIN override
        #      [...] commands x 6 (fixed count)
        # command [0..3] address
        #         [4..5] data
        self._rx.extend(rx_data)
        if len(self._rx) < 3 + (6 * 6):
            return
        self._state.flash_program_we_pin = self._rx[2]
        commands = memoryview(self._rx)[3:]

        self._state.flash_commands.clear()
        for (address, data) in struct.iter_unpack(">IH", commands):
            if address == 0 and data == 0:
                break
            self._state.flash_commands.append((address, data))
        self._is_complete = True
        self._io.lk_response(b"\x01")
