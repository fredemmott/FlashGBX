from ..Command import Command
from ..Interface import Interface
from ..State import State
from ..vars import *

class DmgSetBankChangeCmd(Command):
    command = "DMG_SET_BANK_CHANGE_CMD"

    def __init__(self, fw_vars: dict[int, int], state: State, output: Interface):
        super().__init__(fw_vars, state, output)
        self._rx = bytearray()
        self._is_complete = False

    @property
    def is_complete(self) -> bool:
        return self._is_complete

    def from_lk(self, rx_data: bytes):
        # byte [0] count
        #      [...] commands
        # command [0..3] address
        # command [4..7] data
        self._rx.extend(rx_data)
        if len(self._rx) < 1:
            return
        count = self._rx[0]

        if len(self._rx) < (count * 8) + 1:
            return

        # Not yet implemented, a stub is fine for some cartridges
        self._is_complete = True
        self._io.lk_response(b"\x01")

