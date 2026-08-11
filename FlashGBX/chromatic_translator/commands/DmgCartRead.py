from ..Command import Command
from ..Interface import Interface
from ..State import State
from ..vars import *

class DmgCartRead(Command):
    command = "DMG_CART_READ"

    def __init__(self, fw_vars: dict[int, int], state: State, output: Interface):
        super().__init__(fw_vars, state, output)
        self._is_complete = False
        self._main()

    def from_lk(self, rx_data: bytes):
        raise NotImplementedError("DMG_CART_READ does not receive any data")

    @property
    def is_complete(self) -> bool:
        return self._is_complete

    def _main(self):
        begin = self._fw_vars[VAR_IDX_ADDRESS]
        end = begin + self._fw_vars[VAR_IDX_TRANSFER_SIZE]

        self._fw_vars[VAR_IDX_ADDRESS] = end
        self._io.mc_set_variables(self._fw_vars)

        ret = self._io.mc_exec([(address, 0) for address in range(begin, end)])
        self._is_complete = True
        self._io.lk_response(ret)

