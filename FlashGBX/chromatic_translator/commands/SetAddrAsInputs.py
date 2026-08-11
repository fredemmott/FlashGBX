from ..Command import Command
from ..Interface import Interface
from ..State import State

class SetAddrAsInputs(Command):
    command = "SET_ADDR_AS_INPUTS"

    def __init__(self, fw_vars: dict[int, int], state: State, output: Interface):
        super().__init__(fw_vars, state, output)
        self._io.mc_disable_cart()
        self._io.lk_response(b"\x01")

    @property
    def is_complete(self) -> bool:
        return True

    def from_lk(self, rx_data: bytes):
        raise NotImplementedError("SET_ADDR_AS_INPUTS does not receive any data")

