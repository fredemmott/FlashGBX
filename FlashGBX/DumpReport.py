# -*- coding: utf-8 -*-
# FlashGBX
# Author: Lesserkuma (github.com/Lesserkuma)

import os, platform

class DumpReport:
	@classmethod
	def generate(cls, di, device) -> str:
		from .Mapper import DMG_Mapper, ConvertMapperToMapperType
		from .CartridgeTypes import RomSizes, DmgSaveTypes, AgbSaveTypes
		from .Formatter import Formatter
		from .app import AppInfo
		from . import i18n

		def _fields_to_lines(fields, col=19):
			return [f"* {label + ':':<{col}}{value}" for label, value in fields]

		# Resolve header into a shallow copy so we never mutate the caller's dict
		header = dict(di["header"].get("unchanged", di["header"]))
		if "db" in di["header"]:
			header["db"] = di["header"]["db"]

		mode = di["system"]
		if mode not in ("DMG", "AGB"):
			raise NotImplementedError

		system_name = "Game Boy" if mode == "DMG" else "Game Boy Advance"

		rom_size_int = di["rom_size"]
		if rom_size_int in RomSizes():
			rom_size_str = RomSizes(rom_size_int).GetString(localized=False)
		else:
			rom_size_str = f"{rom_size_int:,} bytes"

		keys = list(device.SUPPORTED_CARTS[mode].keys())
		cart_type_str = keys[di["cart_type"]] if 0 <= di["cart_type"] < len(keys) else f"#{di['cart_type']}"
		file_name = os.path.split(di["file_name"])[1] if di["file_name"] else ""
		file_size_bytes = di["file_size"]
		file_size_str = f"{Formatter.file_size(file_size_bytes, space=' ', localized=False)} ({file_size_bytes:d} bytes)"
		logo_str = "OK" if header["logo_correct"] else "Invalid"

		lines = ["= FlashGBX Dump Report ="]

		lines += ["", "== File Information =="]
		lines += _fields_to_lines([
			("File Name",  file_name),
			("File Size",  file_size_str),
			("CRC32",      f"{di['hash_crc32']:08x}"),
			("MD5",        di["hash_md5"]),
			("SHA-1",      di["hash_sha1"]),
			("SHA-256",    di["hash_sha256"]),
		])

		lines += ["", "== General Information =="]
		general_fields = [
			("Hardware",         f"{device.GetFullName()} – Firmware {device.GetFirmwareVersion()}"),
			("Software",         f"{AppInfo.NAME} {AppInfo.VERSION}"),
			("OS Platform",      f"{AppInfo.os_string()}, {platform.machine()}, {i18n.OS_LANGUAGE}"),
		]
		if device.GetName() == "GBxCart RW": general_fields.append(("Baud Rate", f"{device.GetBaudRate():d}"))
		general_fields += [
			("Dump Time",        di["timestamp"]),
			("Time Elapsed",     "%TIME_ELAPSED% (%TRANSFER_RATE%)"),
			("Transfer Buffer",  f"{di['transfer_size']:d} bytes"),
			("Retries",          f"{device.GetReadErrors():d}"),
		]
		lines += _fields_to_lines(general_fields)

		lines += ["", "== Dumping Settings =="]
		dumping_fields = [
			("Mode",     system_name),
			("ROM Size", rom_size_str),
		]
		if mode == "DMG":
			mapper_int = di["mapper_type"]
			if mapper_int in DMG_Mapper().GetAllMapperIds():
				mapper_str = ConvertMapperToMapperType(mapper_int)[0]
			else:
				mapper_str = f"0x{mapper_int:02X}"
			dumping_fields += [
				("Mapper Type",       mapper_str),
				("Cartridge Profile", cart_type_str),
				("Read Method",       di["dmg_read_method"]),
			]
		else:  # AGB
			dumping_fields += [
				("Cartridge Profile", cart_type_str),
				("Read Method",       di["agb_read_method"]),
			]
		lines += _fields_to_lines(dumping_fields)

		lines += ["", "== Parsed Data =="]

		if mode == "DMG":
			cgb_raw = header["cgb"]
			if cgb_raw == 0xC0:
				target_platform = "Game Boy Color exclusive"
			elif cgb_raw == 0x80:
				target_platform = "Game Boy Color"
			elif header["old_lic"] == 0x33 and header["sgb"] == 0x03:
				target_platform = "Super Game Boy"
			else:
				target_platform = "Original Game Boy"

			sgb_str = "Supported" if (header["old_lic"] == 0x33 and header["sgb"] == 0x03) else "No support"
			cgb_str = DMG_Mapper().CGB_MAP.get(cgb_raw, f"Unknown (0x{cgb_raw:02X})")

			hdr_chk = header["header_checksum"]
			hdr_chk_calc = header.get("header_checksum_calc", hdr_chk)
			header_checksum_str = f"OK (0x{hdr_chk:02X})" if header["header_checksum_correct"] \
				else f"Invalid (0x{hdr_chk_calc:02X}≠0x{hdr_chk:02X})"

			header["rom_checksum_calc"] = device.INFO.get("rom_checksum_calc", header.get("rom_checksum_calc"))
			rom_chk_ok = header["rom_checksum_calc"] == header["rom_checksum"]
			rom_checksum_str = f"OK (0x{header['rom_checksum']:04X})" if rom_chk_ok \
				else f"Invalid (0x{header['rom_checksum_calc']:04X}≠0x{header['rom_checksum']:04X})"

			hdr_rom_size_raw = header["rom_size_raw"]
			if hdr_rom_size_raw < RomSizes().GetNumberOfTypes():
				hdr_rom_size_str = RomSizes().GetString(index=hdr_rom_size_raw, localized=False)
			else:
				hdr_rom_size_str = f"Unknown (0x{hdr_rom_size_raw:02X})"

			hdr_save_raw = header["ram_size_raw"]
			if hdr_save_raw == 0x00:
				hdr_save_str = f"No SRAM (0x{hdr_save_raw:02X})"
			elif hdr_save_raw in DmgSaveTypes():
				hdr_save_str = f"{DmgSaveTypes(mbc=hdr_save_raw).GetString(localized=False)} (0x{hdr_save_raw:02X})"
			else:
				hdr_save_str = f"Unknown (0x{hdr_save_raw:02X})"

			mapper_raw = header["mapper_raw"]
			if mapper_raw in DMG_Mapper().GetAllMapperIds():
				hdr_mapper_str = f"{DMG_Mapper().GetMapperName(mapper_raw)} (0x{mapper_raw:02X})"
			else:
				hdr_mapper_str = f"Unknown (0x{mapper_raw:02X})"

			parsed_fields = [("Game Title", (header.get("game_title") or "").replace("\0", "␀"))]
			if cgb_raw in (0xC0, 0x80) and header.get("game_code"):
				parsed_fields.append(("Game Code", header["game_code"].replace("\0", "␀")))
			parsed_fields += [
				("Revision",        str(header["version"])),
				("Super Game Boy",  sgb_str),
				("Game Boy Color",  cgb_str),
				("Nintendo Logo",   logo_str),
				("Header Checksum", header_checksum_str),
				("ROM Checksum",    rom_checksum_str),
				("ROM Size",        hdr_rom_size_str),
				("SRAM Size",       hdr_save_str),
				("Mapper Type",     hdr_mapper_str),
				("Target Platform", target_platform),
			]
			lines += _fields_to_lines(parsed_fields)

			if "gbmem" in di and di["gbmem"] is not None:
				raw_data = "\n                     ".join(
					''.join(f"{x:02X}" for x in di["gbmem"][i*0x20:i*0x20+0x20])
					for i in range(4)
				)
				if "gbmem_parsed" in di and di["gbmem_parsed"] is not None and len(di["gbmem_parsed"]) > 0:
					if isinstance(di["gbmem_parsed"], list):
						p0 = di["gbmem_parsed"][0]
						lines += ["", "== GB-Memory Data (Multi Menu) =="]
						lines += _fields_to_lines([
							("Write Timestamp", p0["timestamp"]),
							("Write Kiosk ID",  p0["kiosk_id"]),
							("Number of Games", f"{p0['num_games']:d}"),
							("Write Counter",   f"{p0['write_count']:d}"),
							("Cartridge ID",    p0["cart_id"]),
							("Raw Map Data",    raw_data),
						])
						for i in range(1, len(di["gbmem_parsed"])):
							entry = di["gbmem_parsed"][i]
							if entry["menu_index"] == 0xFF or not entry["header"]["logo_correct"]:
								continue
							section = "Menu ROM" if i == 1 else f"Game {i - 1}"
							entry_rom_bytes = entry["rom_size"]
							entry_size_str = f"{Formatter.file_size(entry_rom_bytes, space=' ', localized=False)} ({entry_rom_bytes:d} bytes)"
							entry_fields = [
								("Game Code",        entry["game_code"]),
								("Game Title",       entry["title"]),
								("Write Timestamp",  entry["timestamp"]),
								("Write Kiosk ID",   entry["kiosk_id"]),
								("Location",         f"0x{entry['rom_offset']:06X}–0x{entry['rom_offset'] + entry['rom_size'] - 1:06X}"),
								("ROM Size",         entry_size_str),
							]
							if "crc32" in entry: entry_fields.append(("CRC32",    f"{entry['crc32']:08x}"))
							if "md5"   in entry: entry_fields.append(("MD5",      entry["md5"]))
							if "sha1"  in entry: entry_fields.append(("SHA-1",    entry["sha1"]))
							if "sha256" in entry: entry_fields.append(("SHA-256", entry["sha256"]))
							lines += ["", f"=== {section} ==="]
							lines += _fields_to_lines(entry_fields)
							if "db_entry" in entry and "crc32" in entry and entry["db_entry"]["rc"] == entry["crc32"]:
								lines += _fields_to_lines([("Database Match", f"{entry['db_entry']['gn']} {entry['db_entry']['ne']}")])
					elif isinstance(di["gbmem_parsed"]["game_code"], str):
						p = di["gbmem_parsed"]
						lines += ["", "== GB-Memory Data (Single Game) =="]
						lines += _fields_to_lines([
							("Game Code",       p["game_code"]),
							("Game Title",      p["title"]),
							("Write Timestamp", p["timestamp"]),
							("Write Kiosk ID",  p["kiosk_id"]),
							("Write Counter",   f"{p['write_count']:d}"),
							("Cartridge ID",    p["cart_id"]),
							("Raw Map Data",    raw_data),
						])
				else:
					lines += _fields_to_lines([("GB-Memory Data", raw_data)])

			if header["db"] is not None and header["db"]["rc"] == di["hash_crc32"]:
				db = header["db"]
				db_fields = []
				if "gn" in db and "ne" in db: db_fields.append(("Game Name", f"{db['gn']} {db['ne']}"))
				elif "gn" in db: db_fields.append(("Game Name", db["gn"]))
				if "rg" in db: db_fields.append(("Region", db["rg"]))
				if "lg" in db: db_fields.append(("Language(s)", db["lg"]))
				if "rv" in db: db_fields.append(("Revision", db["rv"]))
				if "gc" in db: db_fields.append(("Game Code", db["gc"]))
				if "rc" in db: db_fields.append(("ROM CRC32", f"{db['rc']:08x}"))
				if "rs" in db: db_fields.append(("ROM Size", Formatter.file_size(db["rs"], space=" ", as_int=True, localized=False)))
				lines += ["", "== Database Match =="]
				lines += _fields_to_lines(db_fields)

		elif mode == "AGB":
			hdr_chk = header["header_checksum"]
			hdr_chk_calc = header.get("header_checksum_calc", hdr_chk)
			header_checksum_str = f"OK (0x{hdr_chk:02X})" if header["header_checksum_correct"] \
				else f"Invalid (0x{hdr_chk_calc:02X}≠0x{hdr_chk:02X})"

			savelib_str = AgbSaveTypes().GetStringFromSaveLib(di["agb_savelib"], localized=False)
			game_title_raw = (header.get("game_title_raw") or "").replace("\0", "␀")
			game_code_raw  = (header.get("game_code_raw")  or "").replace("\0", "␀")

			parsed_fields = [
				("Game Title",      game_title_raw),
				("Game Code",       game_code_raw),
				("Revision",        str(header["version"])),
				("Nintendo Logo",   logo_str),
				("Header Checksum", header_checksum_str),
				("Save Type",       savelib_str),
			]
			if "agb_save_flash_id" in di and di["agb_save_flash_id"] is not None:
				chip_id, chip_name = di["agb_save_flash_id"]
				parsed_fields.append(("Save Flash Chip", f"{chip_name} (0x{chip_id:04X})"))
			if "eeprom_data" in di:
				eeprom_hex = ''.join(f"{x:02X}" for x in di["eeprom_data"])
				parsed_fields.append(("EEPROM area", f"{eeprom_hex}"))
			lines += _fields_to_lines(parsed_fields)

			if cart_type_str == "Vast Fame":
				lines += ["", "== Vast Fame Protection Information =="]
				lines += _fields_to_lines([
					("Address Reordering", str(di.get("vf_addr_reorder", "N/A"))),
					("Value Reordering",   str(di.get("vf_value_reorder", "N/A"))),
				], col=21)

			if header["db"] is not None and header["db"]["rc"] == di["hash_crc32"]:
				db = header["db"]
				db_fields = []
				if "gn" in db and "ne" in db: db_fields.append(("Game Name", f"{db['gn']} {db['ne']}"))
				elif "gn" in db: db_fields.append(("Game Name", db["gn"]))
				if "rg" in db: db_fields.append(("Region", db["rg"]))
				if "lg" in db: db_fields.append(("Language(s)", db["lg"]))
				if "rv" in db: db_fields.append(("Revision", db["rv"]))
				if "gc" in db: db_fields.append(("Game Code", db["gc"]))
				if "rc" in db: db_fields.append(("ROM CRC32", f"{db['rc']:08x}"))
				if "rs" in db: db_fields.append(("ROM Size", Formatter.file_size(db["rs"], space=" ", as_int=True, localized=False)))
				if "st" in db: db_fields.append(("Save Type", AgbSaveTypes(db["st"]).GetString(localized=False)))
				lines += ["", "== Database Match =="]
				lines += _fields_to_lines(db_fields)

		newline = "\r\n" if platform.system() == "Windows" else "\n"
		return newline.join(lines)
