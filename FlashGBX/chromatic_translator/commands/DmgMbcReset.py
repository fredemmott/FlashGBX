from ..Command import Command
from ..Interface import Interface
from ..State import State

class DmgMbcReset(Command):
    command = "DMG_MBC_RESET"

    def __init__(self, fw_vars: dict[int, int], state: State, output: Interface):
        super().__init__(fw_vars, state, output)
        # This sequence should reset an MBC1, MBC3, or MBC5, using the same sequence of commands
        # for all of them; in some cases they have slightly different but useful behaviors, on others,
        # they're ignored
        #
        # Thanks to the gbdev.io pandocs: https://gbdev.io/pandocs/MBCs.html
        #
        # ROM_BANK_SEL_HIGH
        # -----------------
        #
        # MBC1: same as BANK_SEL_LOW, but MBC1 treats 0x00 selection as 0x01
        # MBC3: sets all 7 bits, but also treats 0x00 selection as 0x01
        # MBC5: set high bits of bank
        #
        # ... so, setting to 0 always works :)
        #
        # RAM_BANK_SEL
        # ------------
        #
        # MBC1:
        #  - usually RAM bank select
        #  - also ROM bank number for some MBC multi-cart
        # MBC3: RAM bank selectg
        #
        # BANK_MODE_SEL
        # -------------
        #
        # MBC1: 0 is 'simple' bank 0 ROM+SRAM (default), 1 is 'advanced' (0x4000 register is live)
        commands = [
            (0x0000, 0x00), # RAM_DISABLE
            (0x2000, 0x01), # ROM_BANK_SEL_LOW
            (0x3000, 0x00), # ROM_BANK_SEL_HIGH
            (0x4000, 0x00), # RAM_BANK_SEL
            (0x6000, 0x00), # BANK_MODE_SEL
        ]
        self._io.mc_exec(commands, is_write=True)
        self._io.lk_response(b"\x01")

    def from_lk(self, rx_data: bytes):
        raise NotImplementedError("DMG_MBC_RESET does not receive any data")

    @property
    def is_complete(self) -> bool:
        return True
