from ..Command import Command
from ..Interface import Interface
from ..State import State
from ..vars import *

class DmgCartWriteSRAM(Command):
    command = "DMG_CART_WRITE_SRAM"

    def __init__(self, fw_vars: dict[int, int], state: State, output: Interface):
        super().__init__(fw_vars, state, output)
        self._rx = bytearray()
        self._is_complete = fw_vars.get(VAR_IDX_TRANSFER_SIZE, 0) == 0

    @property
    def is_complete(self) -> bool:
        return self._is_complete

    def from_lk(self, rx_data: bytes):
        self._rx.extend(rx_data)
        if len(self._rx) < self._fw_vars[VAR_IDX_TRANSFER_SIZE]:
            return

        address = self._fw_vars[VAR_IDX_ADDRESS]
        end = address + self._fw_vars[VAR_IDX_TRANSFER_SIZE]
        self._fw_vars[VAR_IDX_ADDRESS] = end
        self._io.mc_set_variables(self._fw_vars)

        commands = [(address + offset, data) for offset, data in enumerate(self._rx)]
        self._io.mc_exec(commands, is_write=True)
        self._is_complete = True
        self._io.lk_response(b"\x01")

