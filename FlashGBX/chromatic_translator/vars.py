from ..LK_Device import LK_Device

def make_var_key(size, key) -> int:
    return (size << 8) | (key & 0xFF)
def lookup_var_key(name: str) -> int:
    (size, key) = LK_Device.DEVICE_VAR[name]
    match size:
        case 8: size = 1
        case 16: size = 2
        case 32: size = 4
        case _: raise ValueError(f"Invalid variable size: {size}")
    return make_var_key(size, key)

VAR_IDX_ADDRESS = lookup_var_key("ADDRESS")
VAR_IDX_TRANSFER_SIZE = lookup_var_key("TRANSFER_SIZE")
VAR_IDX_FLASH_WE_PIN = lookup_var_key("FLASH_WE_PIN")
VAR_IDX_LAST_BANK_ACCESSED = lookup_var_key("LAST_BANK_ACCESSED")
VAR_IDX_FLASH_COMMANDS_BANK_1 = lookup_var_key("FLASH_COMMANDS_BANK_1")
# make_var_key() can not return > 16-bit values
VAR_IDX_HOLD_PIN_AUDIO = 0x10000
