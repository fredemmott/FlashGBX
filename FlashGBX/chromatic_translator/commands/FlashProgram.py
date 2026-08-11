from ..Command import Command
from ..Interface import Interface
from ..State import State
from ..vars import *

class ChromaticCmdFlashProgram(Command):
    command = "FLASH_PROGRAM"

    def __init__(
            self,
            fw_vars: dict[int, int],
            state: State,
            output: Interface):
        super().__init__(fw_vars, state, output)
        self._is_complete = False

        self._address = self._fw_vars[VAR_IDX_ADDRESS]
        self._end = self._address + self._fw_vars[VAR_IDX_TRANSFER_SIZE]

        self._next_vars = self._fw_vars.copy()
        self._next_vars[VAR_IDX_ADDRESS] = self._end
        self._fw_vars[VAR_IDX_FLASH_WE_PIN] = self._state.flash_program_we_pin
        self._io.mc_set_variables(self._fw_vars)

    @property
    def is_complete(self) -> bool:
        return self._is_complete

    def from_lk(self, rx_data: bytes):
        for byte in rx_data:
            self._io.mc_enqueue(
                self._state.flash_commands,
                is_write=True,
                is_flash=True,
                flush=False
            )
            self._io.mc_enqueue(
                [(self._address, byte)],
                is_write=True,
                is_flash=True,
                wait_for_status=True,
                flush=False
            )
            self._address += 1
        if self._address < self._end:
            return
        self._io.mc_exec_enqueued()
        self._io.mc_set_variables(self._next_vars)
        self._is_complete = True
        self._io.lk_response(b"\x01")


