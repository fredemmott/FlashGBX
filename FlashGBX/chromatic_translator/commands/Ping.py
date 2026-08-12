from ..Command import Command
from ..Interface import Interface
from ..State import State

class Ping(Command):
    command = "PING"

    def __init__(self, fw_vars: dict[int, int], state: State, output: Interface):
        super().__init__(fw_vars, state, output)
        self._is_complete = False

    @property
    def is_complete(self) -> bool:
        return self._is_complete

    def from_lk(self, rx_data: bytes):
        # single byte: challenge
        # expected return: ~challenge
        if len(rx_data) != 1:
            return
        response = self._io.mc_ping(rx_data[0])
        self._is_complete = True
        self._io.lk_response(response.to_bytes(1))
