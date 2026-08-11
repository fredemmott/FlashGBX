from abc import ABC

from ..Command import Command
from ..State import State
from ..Interface import Interface

class Stub(Command, ABC):
    def __init__(self, fw_vars: dict[int, int], state: State, output: Interface):
        super().__init__(fw_vars, state, output)
        output.lk_response(b"\x01")

    @property
    def is_complete(self) -> bool:
        return True

    def from_lk(self, rx_data: bytes):
        raise NotImplementedError(f"{self.command} should not receive any data")

class SetModeDmg(Stub):	command = "SET_MODE_DMG"
class SetVoltage3Pt3V(Stub): command = "SET_VOLTAGE_3_3V"
class SetVoltage5V(Stub): command = "SET_VOLTAGE_5V"
class DisablePullups(Stub):	command = "DISABLE_PULLUPS"
