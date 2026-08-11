from typing import Tuple

class State:
    def __init__(self):
        self.flash_commands : list[Tuple[int, int]] = []
        self.flash_program_we_pin: int = 0 # overrides variable