from ..Command import Command
from ..Interface import Interface
from ..State import State

import struct

class CartWriteFlashCmd(Command):
    command = "CART_WRITE_FLASH_CMD"

    def __init__(self, fw_vars: dict[int, int], state: State, output: Interface):
        super().__init__(fw_vars, state, output)
        self._is_complete = False
        self._rx = bytearray()

    @property
    def is_complete(self) -> bool:
        return self._is_complete

    def from_lk(self, rx_data: bytes):
        # byte 0: 'is flashcart' (unused)
        #      1: number of commands
        #      ...: commands
        #
        # command bytes [0..3]: address
        #               [4..5]: data (command)
        self._rx.extend(rx_data)
        if len(self._rx) < 2:
            return
        count = self._rx[1]
        if count == 0:
            return
        if len(self._rx) - 2 < (count * 6):
            return
        payload = memoryview(self._rx)[2:]
        commands = [
            (address, data)
            for (address, data) in struct.iter_unpack(">IH", payload)
        ]
        self._io.mc_exec(commands, is_write=True, is_flash=True)
        self._is_complete = True
        self._io.lk_response(b"\x01")
