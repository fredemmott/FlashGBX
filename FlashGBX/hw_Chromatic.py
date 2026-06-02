# -*- coding: utf-8 -*-
# FlashGBX
# Author: Lesserkuma (github.com/Lesserkuma)
# Author: Fred Emmott

# pylint: disable=wildcard-import, unused-wildcard-import
from .LK_Device import *

from typing import Callable, final, ClassVar, cast, Protocol, Tuple, Iterable, Collection
from abc import ABC, abstractmethod
from dataclasses import dataclass
from queue import SimpleQueue
from collections import deque
import threading

class GbxDevice(LK_Device):
	DEVICE_NAME = "Chromatic"

	def __init__(self):
		pass

	def Initialize(self, flashcarts, port=None, max_baud=2000000):
		if self.IsConnected(): self.DEVICE.close()
		conn_msg = []
		ports = []
		if port is not None:
			ports = [ port ]
		else:
			comports = serial.tools.list_ports.comports()
			for i in range(0, len(comports)):
				if comports[i].vid == 0x374E and comports[i].pid == 0x0101:
					ports.append(comports[i].device)
			if len(ports) == 0: return False

		for i in range(0, len(ports)):
			if self.TryConnect(ports[i], max_baud):
				self.BAUDRATE = max_baud
				dev = serial.Serial(ports[i], self.BAUDRATE, timeout=0.1)
				self.DEVICE = dev
				if self.DEVICE is not None: self.LoadFirmwareVersion()
			else:
				continue

			if self.FW is None or self.FW == {}: continue

			dprint(f"Found a {self.DEVICE_NAME}")
			dprint("Firmware information:", self.FW)
			# dprint("Baud rate:", self.BAUDRATE)

			if self.DEVICE is None or not self.IsConnected():
				self.DEVICE = None
				if self.FW is not None:
					conn_msg.append([0, "Couldn’t communicate with the " + self.DEVICE_NAME + " device on port " + ports[i] + ". Please disconnect and reconnect the device, then try again."])
				continue
			elif self.FW is None:
				dev.close()
				self.DEVICE = None
				continue
			elif "cfw_id" not in self.FW or self.FW["cfw_id"] != 'L': # Not a CFW by FredEmmott
				dprint("Incompatible firmware:", self.FW)
				dev.close()
				self.DEVICE = None
				continue

			self.PORT = ports[i]
			self.DEVICE.timeout = self.DEVICE_TIMEOUT

			conn_msg.append([0, "No help is currently available when using a ModRetro Chromatic device"])

			# Load Flash Cartridge Handlers
			self.UpdateFlashCarts(flashcarts)

			# Stop after first found device
			break

		return conn_msg

	# noinspection PyUnresolvedReferences
	def LoadFirmwareVersion(self):
		dprint("Querying firmware version")
		if self.DEVICE is None: return False
		if not hasattr(self.DEVICE, "_chromatic_fw_version"):
			self.DEVICE._chromatic_fw_version = self._query_firmware_version()
		return self.DEVICE._chromatic_fw_version

	def _query_firmware_version(self):
		try:
			self.DEVICE.timeout = 0.075
			self.DEVICE.reset_input_buffer()
			self.DEVICE.reset_output_buffer()

			self._write(bytearray(b'\x55\xAA'))
			time.sleep(0.01)
			device_id = self.DEVICE.read(self.DEVICE.in_waiting)

			if b"FW L" in device_id:
				dprint("Running dedicated firmware; no longer supported")

			if device_id[0:5] != b"Micro":
				dprint("Not running microcode firmware")
				self.FW = None
				return False

			if device_id != b"Micro2026060101":
				dprint("Running microcode firmware, but not a supported version")
				self.FW = None
				return False

			# TODO: replace "LK" with "MC" after checking it doesn't conflict with MCU
			self._write(bytearray(b'LK')) # Enable LK firmware
			if self.DEVICE.read(1) != b'\xFF':
				dprint("Firmware mode was not enabled successfully")
				self.FW = None
				return False

			# b"Micro2026060101"
			#   012345678901234
			#        ^^^^^^^^
			self.FW["fw_dt"] = device_id[5:13].decode("ascii")
			year = device_id[5:9].decode("ascii")
			month = device_id[9:11].decode("ascii")
			day = device_id[11:13].decode("ascii")
			build = device_id[13:15].decode("ascii")
			self._firmware_version = f"v{year}.{month}.{day}.{build}"

			self.FW["cfw_id"] = "L"
			self.FW["fw_ver"] = 12 # Actually our translator version

			self.FW["pcb_ver"] = None
			self.FW["ofw_ver"] = None
			self.FW["pcb_name"] = "GWA5-25A"


			# Doesn't appear to be physically supported
			self.FW["cart_power_ctrl"] = False
			self.FW["bootloader_reset"] = False

			self.DEVICE_NAME = "Chromatic"

			self.DEVICE.__class__ = type(
				"ChromaticSerialDevice",
				(ChromaticMicrocodeDevice, self.DEVICE.__class__),
				{}
			)
			cast(ChromaticMicrocodeDevice, self.DEVICE).init_chromatic()

			return True

		except Exception as e:
			dprint("Disconnecting due to an error", e, sep="\n")
			try:
				if self.DEVICE.isOpen():
					self.DEVICE.reset_input_buffer()
					self.DEVICE.reset_output_buffer()
					self.DEVICE.close()
				self.DEVICE = None
			except:
				pass
			return False

	def ChangeBaudRate(self, _):
		dprint("Baudrate change is not supported.")

	def CheckActive(self):
		if time.time() < self.LAST_CHECK_ACTIVE + 1: return True
		dprint("Checking if device is active")
		if self.DEVICE is None: return False
		if self.FW["pcb_name"] is None:
			if self.LoadFirmwareVersion():
				self.LAST_CHECK_ACTIVE = time.time()
				return True
			else:
				return False
		try:
			self._get_fw_variable("CART_MODE")
			self.LAST_CHECK_ACTIVE = time.time()
			return True
		except Exception as e:
			dprint("Disconnecting...", e)
			try:
				if self.DEVICE.isOpen():
					self.DEVICE.reset_input_buffer()
					self.DEVICE.reset_output_buffer()
					self.DEVICE.close()
				self.DEVICE = None
			except:
				pass
			return False

	def GetFirmwareVersion(self, more=False):
		return self._firmware_version

	def GetFullNameExtended(self, more=False):
		return "{:s} – Firmware {:s} ({:s})".format(self.GetFullName(), self.GetFirmwareVersion(), self.GetPort())

	def CanSetVoltageManually(self):
		return False

	def CanSetVoltageAutomatically(self):
		return True

	def CanPowerCycleCart(self):
		return self.FW["cart_power_ctrl"]

	def GetSupprtedModes(self):
		return ["DMG"]

	def IsSupported3dMemory(self):
		return False

	def IsClkConnected(self):
		return True

	def SupportsFirmwareUpdates(self):
		return False

	def FirmwareUpdateAvailable(self):
		return False

	def GetFirmwareUpdaterClass(self):
		return None

	def ResetLEDs(self):
		pass

	def SupportsBootloaderReset(self):
		return self.FW["bootloader_reset"]

	def BootloaderReset(self):
		if not self.SupportsBootloaderReset(): return False
		dprint("Resetting to bootloader...")
		try:
			self._write(self.DEVICE_CMD["BOOTLOADER_RESET"], wait=True)
			self._write(1)
			self.Close()
			return True
		except Exception as e:
			print("Disconnecting...", e)
			return False

	def SupportsAudioAsWe(self):
		return True

	def Close(self, cartPowerOff=False):
		if self.DEVICE is None: return
		if self.DEVICE.is_open:
			dprint("Disconnecting from the device")
			self.DEVICE.close()
		self.DEVICE = None
		self.MODE = None

# TODO: var_keys class?
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

# make_var_key() can not return > 16-bit values
VAR_IDX_HOLD_PIN_AUDIO = 0x10000
VAR_IDX_ADDRESS = lookup_var_key("ADDRESS")
VAR_IDX_TRANSFER_SIZE = lookup_var_key("TRANSFER_SIZE")

class ChromaticMicrocodeInterface(Protocol):
	@abstractmethod
	def lk_response(self, data: bytes):
		raise NotImplementedError()

	@abstractmethod
	def mc_disable_cart(self):
		raise NotImplementedError()

	@abstractmethod
	def mc_set_variables(self, fw_vars: dict[int, int]):
		raise NotImplementedError()

	@abstractmethod
	def mc_enqueue(
			self,
			reqs: Collection[Tuple[int, int]],
			is_write: bool = False,
			is_flash: bool = False,
			wait_for_status: bool = False) -> None:
		raise NotImplementedError()

	@abstractmethod
	def mc_poll(self, count: int) -> bytes:
		raise NotImplementedError()

	# enqueue + poll helper
	@abstractmethod
	def mc_exec(
			self,
			reqs: Collection[Tuple[int, int]],
			is_write: bool = False,
			is_flash: bool = False,
			wait_for_status: bool = False) -> bytes:
		raise NotImplementedError()

	@abstractmethod
	def mc_ping(self) -> None:
		raise NotImplementedError()

	@abstractmethod
	def mc_wait_for_ack(self) -> None:
		raise NotImplementedError()

class ChromaticMicrocodeCommand(ABC):
	_opcode : ClassVar[int|None] = None

	def __init__(
			self,
			fw_vars: dict[int, int],
			output: ChromaticMicrocodeInterface):
		self._fw_vars = fw_vars
		self._io = output


	@classmethod
	@abstractmethod
	def command(cls) -> str:
		raise NotImplementedError()

	@classmethod
	@final
	def opcode(cls) -> int:
		if cls._opcode is None:
			opcode = cls._opcode = LK_Device.DEVICE_CMD[cls.command]
			cls._opcode = opcode
			return opcode
		return cls._opcode

	@abstractmethod
	def from_lk(self, data: bytes):
		raise NotImplementedError()

	@property
	@abstractmethod
	def is_complete(self) -> bool:
		raise NotImplementedError()

# Mixin for USB serial device
# Converts LK_Device serial commands to Chromatic Microcode
#
# TODO:
# - explain microcode decision
# - composition over inheritance? more work, maybe less brittle
# - deques of bytes | bytearray
class ChromaticMicrocodeDevice(serial.Serial, ChromaticMicrocodeInterface):
	@dataclass(frozen=True)
	class ShutdownSignal:
		pass

	_command: ChromaticMicrocodeCommand | None
	_lk_request_queue: SimpleQueue[bytes|ShutdownSignal]
	_lk_response_deque: deque[int|NotImplementedError]
	_lk_response_condition: threading.Condition

	_vars: dict[int, int]

	_commands: dict[int, type[ChromaticMicrocodeCommand]]

	_executor_thread: threading.Thread

	@classmethod
	def _typed_subclasses(cls, root: type[ChromaticMicrocodeCommand]) -> list[type[ChromaticMicrocodeCommand]]:
		return root.__subclasses__()

	@classmethod
	def _all_commands(cls, root: type[ChromaticMicrocodeCommand]):
		klasses = set(cls._typed_subclasses(root))
		while True:
			before = len(klasses)
			for sub in list(klasses):
				klasses.update(cls._typed_subclasses(sub))
			if len(klasses) == before:
				return [klass for klass in klasses if not klass.__abstractmethods__]

	def init_chromatic(self):
		self._command = None
		self._lk_request_queue = SimpleQueue()
		self._lk_response_deque = deque()
		self._lk_response_condition = threading.Condition()

		self._vars = {}

		self._commands = {}
		for klass in self._all_commands(ChromaticMicrocodeCommand):
			self._commands[klass.opcode()] = klass

		self._executor_thread = threading.Thread(target=self._executor_main, daemon=True, name="ChromaticMicrocode")
		self._executor_thread.start()

	def close(self):
		self._lk_request_queue.put(self.ShutdownSignal())
		self._executor_thread.join()
		super().close()

	def read(self, size = 1) -> bytearray:
		timeout = super().timeout
		timeout_at = (time.monotonic() + timeout) if timeout is not None else None
		ret = bytearray()
		while len(ret) < size:
			remaining = size - len(ret)
			with self._lk_response_condition:
				while not self._lk_response_deque:
					if timeout_at is None:
						self._lk_response_condition.wait()
					else:
						timeout = timeout_at - time.monotonic()
						if timeout <= 0:
							return ret
						self._lk_response_condition.wait(timeout)
						if not self._lk_response_deque:
							return ret
				available  = len(self._lk_response_deque)
				to_read = min(remaining, available)
				items = [self._lk_response_deque.popleft() for _ in range(to_read)]
				for item in items:
					if isinstance(item, Exception):
						raise item
					ret.append(item)
		return ret

	def write(self, data):
		if not isinstance(data, (bytes, bytearray)):
			raise NotImplementedError()
		self._lk_request_queue.put(bytes(data))
		return len(data)

	@property
	def in_waiting(self):
		with self._lk_response_condition:
			return len(self._lk_response_deque)

	def usb_write(self, data):
		super().write(data)
	def usb_read(self, size) -> bytes:
		return super().read(size)
	def usb_read_byte(self):
		ret = self.usb_read(1)
		return ret[0] if ret else None

	@property
	def usb_in_waiting(self):
		return super().in_waiting

	def lk_response(self, data: bytes):
		with self._lk_response_condition:
			self._lk_response_deque.extend(data)
			self._lk_response_condition.notify()

	def mc_enqueue(
			self,
			reqs: Collection[Tuple[int, int]],
			is_write: bool = False,
			is_flash: bool = False,
			wait_for_status: bool = False) -> None:
		buffer = struct.pack("BB", 0x02, len(reqs))
		# Verilog:
		#
		#     ENQUEUE_RX_BYTE_0: begin
		#         req_o.address[15:8] <= rx_data_r;
		#         ENQUEUE_remaining <= ENQUEUE_remaining - 8'd1;
		#     end
		#     ENQUEUE_RX_BYTE_1: req_o.address[7:0] <= rx_data_r;
		#     ENQUEUE_RX_BYTE_2: req_o.data <= rx_data_r;
		#     ENQUEUE_RX_BYTE_3: begin
		#         enqueue_o <= 1'b1;
		#         req_o.is_write <= rx_data_r[0];
		#         req_o.is_flash <= rx_data_r[1];
		#         req_o.wait_for_status <= rx_data_r[2];
		#     end
		buffer += b"".join(
			struct.pack(
			">HBB",
			address,
				data,
				is_write << 0
				| is_flash << 1
				| wait_for_status << 2)
			for (address, data) in reqs
		)
		self.usb_write(buffer)
		self.mc_wait_for_ack()

	def mc_disable_cart(self):
		self.usb_write(b"\x00")
		self.mc_wait_for_ack()

	def mc_set_variables(self, fw_vars: dict[int, int]):
		self._vars = fw_vars
		# Verilog:
		#
		#     vars_o <= '{
		#         flash_we_pin: rx_data_r[1:0],
		#         hold_pin_audio: rx_data_r[2],
		#         dmg_read_cs_pulse: rx_data_r[3],
		#         dmg_write_cs_pulse: rx_data_r[4]
		#     };
		value = self._vars.get(lookup_var_key("FLASH_WE_PIN"), 0) & 0b11
		value = value | (self._vars.get(VAR_IDX_HOLD_PIN_AUDIO, 0) << 2)
		value = value | (self._vars.get(lookup_var_key("DMG_READ_CS_PULSE"), 0) << 3)
		value = value | (self._vars.get(lookup_var_key("DMG_WRITE_CS_PULSE"), 0) << 4)

		buffer = struct.pack("BB", 0x01, value)
		self.usb_write(buffer)
		self.mc_wait_for_ack()

	def mc_poll(self, count: int) -> bytes:
		command = struct.pack("BB", 0x03, count)
		self.usb_write(command)
		ret = self.usb_read(count)
		return ret

	def mc_exec(
			self,
			reqs: Collection[Tuple[int, int]],
			is_write: bool = False,
			is_flash: bool = False,
			wait_for_status: bool = False) -> bytes:
		self.mc_enqueue(reqs, is_write, is_flash, wait_for_status)
		ret = self.mc_poll(len(reqs))
		return ret

	def mc_ping(self) -> None:
		self.usb_write(b"\x04")
		self.mc_wait_for_ack()

	def mc_wait_for_ack(self):
		ret = self.usb_read_byte()
		if ret != 0x01:
			raise Exception("Expected ACK, device response was not 0x01")

	def _executor_main(self):
		while self.is_open:
			buf = self._lk_request_queue.get()
			if isinstance(buf, self.ShutdownSignal):
				return

			if self._command is not None and self._command.is_complete:
				self._command = None

			if self._command is not None:
				self._command.from_lk(buf)
				continue

			if buf[0] in self._commands:
				klass = self._commands[buf[0]]
				self._command = klass(self._vars.copy(), self)
			else:
				keys = [k for k, v in LK_Device.DEVICE_CMD.items() if v == buf[0]]
				if len(keys) == 1:
					cmd = f"{keys[0]} (0x{buf[0]:02X})"
				else:
					cmd = f"Unknown command (0x{buf[0]:02X})"
				with self._lk_response_condition:
					self._lk_response_deque.append(NotImplementedError(cmd))
					self._lk_response_condition.notify()
			if len(buf) > 1:
				cast(ChromaticMicrocodeCommand, self._command).from_lk(buf[1:])

class ChromaticCmdGetVariable(ChromaticMicrocodeCommand):
	command = "GET_VARIABLE"

	def __init__(self, fw_vars: dict[int, int], output: ChromaticMicrocodeInterface):
		super().__init__(fw_vars, output)
		self._is_complete = False
		self._rx = bytearray()

	@property
	def is_complete(self) -> bool:
		return self._is_complete

	def from_lk(self, data: bytes):
		self._rx.extend(data)
		if len(self._rx) < 5:
			return
		# byte [0]    size
		#      [1..4] key (first 3 bytes unused)
		key = make_var_key(self._rx[0], self._rx[4])
		value = self._fw_vars.get(key, 0)
		response = struct.pack(">I", value)
		self._io.lk_response(response)
		self._is_complete = True

class ChromaticCmdSetVariable(ChromaticMicrocodeCommand):
	command = "SET_VARIABLE"

	def __init__(self, fw_vars: dict[int, int], output: ChromaticMicrocodeInterface):
		super().__init__(fw_vars, output)
		self._is_complete = False
		self._rx = bytearray()

	@property
	def is_complete(self) -> bool:
		return self._is_complete

	def from_lk(self, data: bytes):
		self._rx.extend(data)
		if len(self._rx) < 7:
			return
		# byte [0]    size
		#      [1..4] key (first 3 bytes unused)
		#      [5..8] value
		key = make_var_key(self._rx[0], self._rx[4])
		value = struct.unpack(">I", self._rx[5:9])[0]
		self._fw_vars[key] = value
		self._io.mc_set_variables(self._fw_vars)
		self._io.lk_response(b"\x01")
		self._is_complete = True

class ChromaticCmdSetPin(ChromaticMicrocodeCommand):
	command = "SET_PIN"

	def __init__(self, fw_vars: dict[int, int], output: ChromaticMicrocodeInterface):
		super().__init__(fw_vars, output)
		self._is_complete = False
		self._rx = bytearray()

	@property
	def is_complete(self) -> bool:
		return self._is_complete

	def from_lk(self, data: bytes):
		# byte [0..3] pin bits
		#      [4]    value (0 or 1)
		self._rx.extend(data)
		if len(self._rx) < 5:
			return
		bits = struct.unpack(">I", self._rx[0:4])[0]
		value = self._rx[4]
		if bits & (1 << 30):
			self._fw_vars[VAR_IDX_HOLD_PIN_AUDIO] = value
		self._io.mc_set_variables(self._fw_vars)
		self._io.lk_response(b"\x01")
		self._is_complete = True

class ChromaticCmdSetAddrAsInputs(ChromaticMicrocodeCommand):
	command = "SET_ADDR_AS_INPUTS"

	def __init__(self, fw_vars: dict[int, int], output: ChromaticMicrocodeInterface):
		super().__init__(fw_vars, output)
		self._io.mc_disable_cart()
		self._io.lk_response(b"\x01")

	@property
	def is_complete(self) -> bool:
		return True

	def from_lk(self, data: bytes):
		raise NotImplementedError("SET_ADDR_AS_INPUTS does not receive any data")


class ChromaticCmdDmgMbcReset(ChromaticMicrocodeCommand):
	command = "DMG_MBC_RESET"

	def __init__(self, fw_vars: dict[int, int], output: ChromaticMicrocodeInterface):
		super().__init__(fw_vars, output)
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

	def from_lk(self, data: bytes):
		raise NotImplementedError("DMG_MBC_RESET does not receive any data")

	@property
	def is_complete(self) -> bool:
		return True

class ChromaticCmdDmgCartRead(ChromaticMicrocodeCommand):
	command = "DMG_CART_READ"

	def __init__(self, fw_vars: dict[int, int], output: ChromaticMicrocodeInterface):
		super().__init__(fw_vars, output)
		self._is_complete = False
		self._main()

	def from_lk(self, data: bytes):
		raise NotImplementedError("DMG_CART_READ does not receive any data")

	@property
	def is_complete(self) -> bool:
		return self._is_complete

	def _main(self):
		begin = self._fw_vars[VAR_IDX_ADDRESS]
		end = begin + self._fw_vars[VAR_IDX_TRANSFER_SIZE]

		self._fw_vars[VAR_IDX_ADDRESS] = end
		self._io.mc_set_variables(self._fw_vars)

		pending = 0
		chunk = list()
		for addr in range(begin, end):
			if pending == 512:
				data = self._io.mc_poll(0xFF)
				self._io.lk_response(data)
				pending -= len(data)

			chunk.append((addr, 0))

			if len(chunk) == 0xFF:
				self._io.mc_enqueue(chunk)
				chunk = list()
			pending += 1

		if chunk:
			self._io.mc_enqueue(chunk)

		while pending > 0:
			data = self._io.mc_poll(min(pending, 0xFF))
			pending -= len(data)
			self._io.lk_response(data)

		self._is_complete = True

class ChromaticCmdDmgCartWrite(ChromaticMicrocodeCommand):
	command = "DMG_CART_WRITE"

	def __init__(self, fw_vars: dict[int, int], output: ChromaticMicrocodeInterface):
		super().__init__(fw_vars, output)
		self._is_complete = False
		self._rx = bytearray()

	@property
	def is_complete(self) -> bool:
		return self._is_complete

	def from_lk(self, data: bytes):
		# byte [0..3] address
		#      [4]    value
		self._rx.extend(data)
		if len(self._rx) < 5:
			return
		addr = struct.unpack(">I", self._rx[0:4])[0]
		value = self._rx[4]
		self._io.mc_exec([(addr, value)], is_write=True)
		self._io.lk_response(b"\x01")
		self._is_complete = True

class ChromaticCmdStub(ChromaticMicrocodeCommand, ABC):
	def __init__(self, fw_vars: dict[int, int], output: ChromaticMicrocodeInterface):
		super().__init__(fw_vars, output)
		output.lk_response(b"\x01")

	@property
	def is_complete(self) -> bool:
		return True

	def from_lk(self, data: bytes):
		raise NotImplementedError(f"{self.command} should not receive any data")

class ChromaticCmdSetModeDmg(ChromaticCmdStub):	command = "SET_MODE_DMG"
class ChromaticCmdSetVoltage3Pt3V(ChromaticCmdStub): command = "SET_VOLTAGE_3_3V"
class ChromaticCmdSetVoltage5V(ChromaticCmdStub): command = "SET_VOLTAGE_5V"
class ChromaticCmdDisablePullups(ChromaticCmdStub):	command = "DISABLE_PULLUPS"