from ..Command import Command
from ..Interface import Interface
from ..State import State

import struct

class DmgCartWrite(Command):
    command = "DMG_CART_WRITE"
    is_flash = False

    def __init__(self, fw_vars: dict[int, int], state: State, output: Interface):
        super().__init__(fw_vars, state, output)
        self._is_complete = False
        self._rx = bytearray()

    @property
    def is_complete(self) -> bool:
        return self._is_complete

    def from_lk(self, rx_data: bytes):
        # byte [0..3] address
        #      [4]    value
        self._rx.extend(rx_data)
        if len(self._rx) < 5:
            return
        addr = struct.unpack(">I", self._rx[0:4])[0]
        value = self._rx[4]
        self._io.mc_exec([(addr, value)], is_write=True, is_flash=self.is_flash)
        self._io.lk_response(b"\x01")
        self._is_complete = True

class DmgFlashWriteByte(DmgCartWrite):
    command = "DMG_FLASH_WRITE_BYTE"
    is_flash = True

