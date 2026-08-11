# -*- coding: utf-8 -*-
# FlashGBX
# Author: Lesserkuma (github.com/Lesserkuma)

from .i18n import __, c__

class RomSizes:
	ROM_SIZES = [2**i for i in range(15, 30)] # 32 KiB to 512 MiB
	ROM_SIZES_DMG = [2**i for i in range(15, 28)] # 32 KiB to 128 MiB

	def __init__(self, size=None, index=None):
		if index is not None:
			self._size = self.GetSize(index)
		elif size is not None:
			self._size = size
		else:
			self._size = None

	def __contains__(self, size):
		return size in self.ROM_SIZES

	def GetString(self, size=None, index=None, localized=True):
		if index is not None:
			if not (0 <= index < len(self.ROM_SIZES)): return ""
			size = self.ROM_SIZES[index]
		elif size is None:
			size = self._size

		if size is None:
			size = 0

		bytes = __(" Bytes") if localized else " Bytes"
		kib = __(" KiB") if localized else " KiB"
		mib = __(" MiB") if localized else " MiB"

		if size >= 1024 * 1024:
			value = size // (1024 * 1024)
			return f"{value}{mib}"
		elif size >= 1024:
			value = size // 1024
			return f"{value}{kib}"
		else:
			return f"{size}{bytes}"

	def GetNextLarger(self, size):
		for valid_size in self.ROM_SIZES:
			if valid_size >= size:
				return valid_size
		return None

	def GetSize(self, index):
		if index < len(self.ROM_SIZES):
			return self.ROM_SIZES[index]
		return None

	def GetIndex(self, size):
		if size in self.ROM_SIZES:
			return self.ROM_SIZES.index(size)
		return None

	def GetStringList(self, mode="AGB"):
		if mode == "DMG":
			return [self.GetString(index=index) for index in range(len(self.ROM_SIZES_DMG))]
		elif mode == "AGB":
			return [self.GetString(index=index) for index in range(len(self.ROM_SIZES))]
		return []

	def GetNumberOfTypes(self, mode="AGB"):
		if mode == "DMG":
			return len(self.ROM_SIZES_DMG)
		return len(self.ROM_SIZES)

	@classmethod
	def _SizeToCLIName(cls, size):
		if size >= 1024 * 1024:
			return f"{size // (1024 * 1024)}mb"
		return f"{size // 1024}kb"

	@classmethod
	def GetCLINames(cls, mode="AGB", include_auto=True):
		sizes = cls.ROM_SIZES_DMG if mode == "DMG" else cls.ROM_SIZES
		names = [cls._SizeToCLIName(s) for s in sizes]
		return (["auto"] + names) if include_auto else names

	@classmethod
	def GetSizeFromCLIName(cls, name, mode="AGB"):
		if name == "auto":
			return None
		sizes = cls.ROM_SIZES_DMG if mode == "DMG" else cls.ROM_SIZES
		for s in sizes:
			if cls._SizeToCLIName(s) == name:
				return s
		return None


class AgbSaveTypes:
	SAVE_TYPES = [
		(0, ""),
		(512, "4K EEPROM"),
		(8192, "64K EEPROM"),
		(32768, "256K SRAM/FRAM"),
		(65536, "512K FLASH"),
		(131072, "1M FLASH"),
		(1048576, "8M DACS"),
		(65536, "Unlicensed 512K SRAM"),
		(131072, "Unlicensed 1M SRAM"),
		(0, "Unlicensed Batteryless SRAM")
	]

	# CLI shorthand for each SAVE_TYPES entry. None means the entry has no CLI alias.
	CLI_NAMES = [
		None,           # 0
		"eeprom4k",     # 1
		"eeprom64k",    # 2
		"sram256k",     # 3
		"flash512k",    # 4
		"flash1m",      # 5
		"dacs8m",       # 6
		"sram512k",     # 7
		"sram1m",       # 8
		"batteryless",  # 9
	]

	AGB_FLASH_SAVE_CHIPS = {
		0xBFD4: ("SST 39VF512", 0x10000),
		0x1F3D: ("Atmel AT29LV512", 0x10000),
		0xC21C: ("Macronix MX29L512", 0x10000),
		0x321B: ("Panasonic MN63F805MNP", 0x10000),
		0xC209: ("Macronix MX29L010", 0x20000),
		0x6213: ("SANYO LE26FV10N1TS", 0x20000),
		0xBF4B: ("Unlicensed SST25VF064C", 0x20000),
		0xBF5B: ("Unlicensed SST49LF080A", 0x20000),
		0xBF6D: ("Unlicensed SST39VF6401B", 0x20000),
		0xFFFF: ("Unlicensed 0xFFFF", 0x20000)
	}

	def __init__(self, index=None):
		self._index = index

	def __contains__(self, size):
		return size in [s[0] for s in self.SAVE_TYPES]

	def GetName(self, index=None):
		if index is None:
			index = self._index
		if index is not None and 0 <= index < len(self.SAVE_TYPES):
			return self.SAVE_TYPES[index][1]
		return c__("Game Data", "Unknown")

	def GetSize(self, index=None):
		if index is None:
			index = self._index
		if index is not None and 0 <= index < len(self.SAVE_TYPES):
			return self.SAVE_TYPES[index][0]
		return None

	def GetString(self, index=None, localized=True):
		if index is None:
			index = self._index

		if index is None or index < 0 or index >= len(self.SAVE_TYPES):
			return c__("Game Data", "Unknown")

		name = self.GetName(index)
		if name == "":
			name = c__("Save Type", "None") if localized else "None"
		bytes_val = self.GetSize(index)

		if bytes_val == 0:
			return name

		bytes = __(" Bytes") if localized else " Bytes"
		kib = __(" KiB") if localized else " KiB"
		mib = __(" MiB") if localized else " MiB"

		if bytes_val >= 1024 * 1024:
			return f"{name} ({bytes_val >> 20}{mib})"
		elif bytes_val >= 1024:
			return f"{name} ({bytes_val >> 10}{kib})"
		else:
			return f"{name} ({bytes_val}{bytes})"

	def GetIndexFromSize(self, size):
		for idx, (bytes_val, _) in enumerate(self.SAVE_TYPES):
			if bytes_val == size:
				return idx
		return None

	def GetStringFromSaveLib(self, savelib_string, localized=True):
		if not savelib_string or savelib_string == "N/A":
			return __("None") if localized else "None"
		elif "SRAM_F_" in savelib_string:
			return "256K SRAM/FRAM ({:s})".format(savelib_string)
		elif "SRAM_" in savelib_string:
			return "256K SRAM ({:s})".format(savelib_string)
		elif "EEPROM_V" in savelib_string:
			return "4K or 64K EEPROM ({:s})".format(savelib_string)
		elif "FLASH_V" in savelib_string or "FLASH512_V" in savelib_string:
			return "512K FLASH ({:s})".format(savelib_string)
		elif "FLASH1M_V" in savelib_string:
			return "1M FLASH ({:s})".format(savelib_string)
		elif "AGB_8MDACS_DL_V" in savelib_string:
			return "8M DACS ({:s})".format(savelib_string)
		else:
			return c__("Save Type", "Unknown") + " ({:s})".format(savelib_string) if localized else "Unknown ({:s})".format(savelib_string)

	def GetStringList(self):
		return [self.GetString(index) for index in range(len(self.SAVE_TYPES))]

	def GetNumberOfTypes(self):
		return len(self.SAVE_TYPES)

	def GetFlashChipName(self, chip_index):
		if not self.IsValidFlashChipIndex(chip_index): return "Unknown"
		return self.AGB_FLASH_SAVE_CHIPS[chip_index][0]

	def GetFlashChipSize(self, chip_index):
		if not self.IsValidFlashChipIndex(chip_index): return 0
		return self.AGB_FLASH_SAVE_CHIPS[chip_index][1]

	def IsValidFlashChipIndex(self, chip_index):
		return chip_index in self.AGB_FLASH_SAVE_CHIPS.keys()

	@classmethod
	def GetCLINames(cls, include_auto=True):
		out = ["auto"] if include_auto else []
		out.extend(name for name in cls.CLI_NAMES if name is not None)
		return out

	@classmethod
	def GetIndexFromCLIName(cls, name):
		if name == "auto":
			return None
		if name in cls.CLI_NAMES:
			return cls.CLI_NAMES.index(name)
		return None

class DmgSaveTypes:
	RAM_TYPES = [
		(0x00, 0, __("None")),
		(0x100, 0x200, "4K SRAM"),
		(0x01, 0x800, "16K SRAM"),
		(0x02, 0x2000, "64K SRAM"),
		(0x03, 0x8000, "256K SRAM"),
		(0x05, 0x10000, "512K SRAM"),
		(0x04, 0x20000, "1M SRAM"),
		(0x104, 0x108000, "MBC6 SRAM+FLASH"),
		(0x101, 0x100, "MBC7 2K EEPROM"),
		(0x102, 0x200, "MBC7 4K EEPROM"),
		(0x103, 0x20, "TAMA5 EEPROM"),
		(0x201, 0x80000, "Unlicensed 4M SRAM"),
		(0x203, 0x20000, "Unlicensed 1M EEPROM"),
		(0x204, 0x100000, "Unlicensed Photo! Directory"),
		(0x205, 0, "Unlicensed Batteryless SRAM"),
	]

	# CLI shorthand → mbc id. The 0x00 "None" entry deliberately has no shorthand
	# (argparse "auto" covers that case).
	CLI_NAMES = {
		0x100: "4k",
		0x01:  "16k",
		0x02:  "64k",
		0x03:  "256k",
		0x05:  "512k",
		0x04:  "1m",
		0x104: "mbc6",
		0x101: "mbc7_2k",
		0x102: "mbc7_4k",
		0x103: "tama5",
		0x201: "sram4m",
		0x203: "eeprom1m",
		0x204: "photo",
		0x205: "batteryless",
	}

	def __init__(self, size=None, index=None, mbc=None):
		if index is not None:
			self._entry = self.RAM_TYPES[index] if 0 <= index < len(self.RAM_TYPES) else None
		elif mbc is not None:
			self._entry = self._FindByMbc(mbc)
		elif size is not None:
			self._entry = self._FindBySize(size)
		else:
			self._entry = None

	def _FindByMbc(self, mbc):
		for entry in self.RAM_TYPES:
			if entry[0] == mbc:
				return entry
		return None

	def _FindBySize(self, size):
		for entry in self.RAM_TYPES:
			if entry[1] == size:
				return entry
		return None

	def GetName(self):
		return self._entry[2] if self._entry else __("Unknown Save Type")

	def GetSize(self):
		return self._entry[1] if self._entry else 0

	def GetMbc(self):
		return self._entry[0] if self._entry else None

	def GetIndex(self):
		if self._entry is None:
			return None
		for index, entry in enumerate(self.RAM_TYPES):
			if entry == self._entry:
				return index
		return None

	def GetString(self, index=None, localized=True):
		entry = self.RAM_TYPES[index] if (index is not None and 0 <= index < len(self.RAM_TYPES)) else self._entry
		name = entry[2] if entry else __("Unknown Save Type")
		size = entry[1] if entry else 0

		if size == 0:
			return name

		bytes = __(" Bytes") if localized else " Bytes"
		kib = __(" KiB") if localized else " KiB"
		mib = __(" MiB") if localized else " MiB"

		if size == 0x108000:
			return f"{name} ({size / (1024 * 1024):.2f}{mib})"
		elif size >= 1024 * 1024:
			return f"{name} ({size // (1024 * 1024)}{mib})"
		elif size >= 1024:
			return f"{name} ({size // 1024}{kib})"
		else:
			return f"{name} ({size}{bytes})"

	def GetStringList(self):
		return [self.GetString(index=index) for index in range(len(self.RAM_TYPES))]

	def GetNumberOfTypes(self):
		return len(self.RAM_TYPES)

	def __contains__(self, item):
		if isinstance(item, DmgSaveTypes):
			mbc = item.GetMbc()
		else:
			mbc = item
		return self._FindByMbc(mbc) is not None

	@classmethod
	def GetCLINames(cls, include_auto=True, include_batteryless=True):
		# Names are listed in RAM_TYPES order so the CLI menu is stable.
		out = ["auto"] if include_auto else []
		for entry in cls.RAM_TYPES:
			mbc = entry[0]
			if mbc == 0x205 and not include_batteryless:
				continue
			name = cls.CLI_NAMES.get(mbc)
			if name is not None:
				out.append(name)
		return out

	@classmethod
	def GetMbcFromCLIName(cls, name):
		if name == "auto":
			return None
		for mbc, n in cls.CLI_NAMES.items():
			if n == name:
				return mbc
		return None
