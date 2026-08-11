from ..Command import Command
from ..Interface import Interface
from ..State import State
from ..vars import *

import struct

class SetPin(Command):
    command = "SET_PIN"

    def __init__(self, fw_vars: dict[int, int], state: State, output: Interface):
        super().__init__(fw_vars, state, output)
        self._is_complete = False
        self._rx = bytearray()

    @property
    def is_complete(self) -> bool:
        return self._is_complete

    def from_lk(self, rx_data: bytes):
        # byte [0..3] pin bits
        #      [4]    value (0 or 1)
        self._rx.extend(rx_data)
        if len(self._rx) < 5:
            return
        bits = struct.unpack(">I", self._rx[0:4])[0]
        value = self._rx[4]
        if bits & (1 << 30):
            self._fw_vars[VAR_IDX_HOLD_PIN_AUDIO] = value
        self._io.mc_set_variables(self._fw_vars)
        self._io.lk_response(b"\x01")
        self._is_complete = True
