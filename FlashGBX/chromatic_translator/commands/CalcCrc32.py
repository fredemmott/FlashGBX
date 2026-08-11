from ..Command import Command
from ..Interface import Interface
from ..State import State
from ..vars import *

import struct
import zlib

class CalcCrc32(Command):
    command = "CALC_CRC32"

    def __init__(self, fw_vars: dict[int, int], state: State, output: Interface):
        super().__init__(fw_vars, state, output)
        self._rx = bytearray()
        self._is_complete = False

    @property
    def is_complete(self) -> bool:
        return self._is_complete

    def from_lk(self, rx_data: bytes):
        self._rx.extend(rx_data)
        if len(self._rx) < 4:
            return
        count = struct.unpack(">I", self._rx[0:4])[0]
        begin = self._fw_vars[VAR_IDX_ADDRESS]
        end = begin + count

        reqs = [
            (address, 0)
            for address in range(begin, end)
        ]
        data = self._io.mc_exec(reqs)
        crc = zlib.crc32(data)

        self._io.lk_response(struct.pack(">I", crc))
        self._is_complete = True

