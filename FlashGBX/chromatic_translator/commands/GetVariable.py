from ..Command import Command
from ..Interface import Interface
from ..State import State
from ..vars import *

import struct

class GetVariable(Command):
    command = "GET_VARIABLE"

    def __init__(self, fw_vars: dict[int, int], state: State, output: Interface):
        super().__init__(fw_vars, state, output)
        self._is_complete = False
        self._rx = bytearray()

    @property
    def is_complete(self) -> bool:
        return self._is_complete

    def from_lk(self, rx_data: bytes):
        self._rx.extend(rx_data)
        if len(self._rx) < 5:
            return
        # byte [0]    size
        #      [1..4] key (first 3 bytes unused)
        key = make_var_key(self._rx[0], self._rx[4])
        value = self._fw_vars.get(key, 0)
        response = struct.pack(">I", value)
        self._io.lk_response(response)
        self._is_complete = True

