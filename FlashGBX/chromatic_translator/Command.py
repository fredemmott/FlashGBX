from abc import ABC, abstractmethod
from typing import ClassVar, final

from .State import State
from .Interface import Interface
from ..LK_Device import LK_Device

class Command(ABC):
    _opcode : ClassVar[int|None] = None

    def __init__(
            self,
            fw_vars: dict[int, int],
            state: State,
            output: Interface):
        self._fw_vars = fw_vars
        self._state = state
        self._io = output


    @classmethod
    @abstractmethod
    def command(cls) -> str:
        raise NotImplementedError()

    @classmethod
    @final
    def opcode(cls) -> int:
        return LK_Device.DEVICE_CMD[cls.command]

    @abstractmethod
    def from_lk(self, rx_data: bytes):
        raise NotImplementedError()

    @property
    @abstractmethod
    def is_complete(self) -> bool:
        raise NotImplementedError()
