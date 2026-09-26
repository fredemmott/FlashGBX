# -*- coding: utf-8 -*-
# FlashGBX
# Author: Lesserkuma (github.com/Lesserkuma)
# Author: Fred Emmott
import locale
import sys
from pathlib import Path
import sysconfig
import ctypes
import zipfile
import tempfile
import atexit

# pylint: disable=wildcard-import, unused-wildcard-import
from .LK_Device import *
from .LK_Chromatic import Device as MicrocodeDevice

from typing import cast

from .Logging import dprint
from .app import AppContext

NATIVE_STRING_CALLBACK = ctypes.CFUNCTYPE(
    None,  # return void
    ctypes.POINTER(ctypes.c_uint8),  # uint8_t* data
    ctypes.c_uint16  # uint16_t len
)
NATIVE_PROGRESS_CALLBACK = ctypes.CFUNCTYPE(
    None,
    ctypes.c_size_t,
    ctypes.c_size_t
)

pyside = None
try:
    from . import pyside
except ImportError:
    pass


class GbxDevice(LK_Device):
    DEVICE_NAME = "Chromatic"
    REQUIRED_FW_VERSION = "2026.09.25.0"

    DEVICE : MicrocodeDevice | None = None

    USB_VENDOR_ID = 0x374e
    USB_PRODUCT_ID = 0x0101

    PORT = f"libusb:{USB_VENDOR_ID:04x}:{USB_PRODUCT_ID:04x}"

    # - Align with 512-byte USB packet sizes
    # - Fit in a uint16 as the LK protocol requires it
    MAX_BUFFER_READ = 0x10000 - 512
    MAX_BUFFER_WRITE = MAX_BUFFER_READ

    _c_callbacks = []

    def __init__(self):
        super().__init__()
        self._load_lk()

    def _load_lk(self):
        ext = ".dll" if sys.platform == "win32" else ".so"
        name = f"_LK_Chromatic{ext}"
        # Support running from pyinstaller on macOS
        if getattr(sys, 'frozen', False):
            base_path = getattr(sys, '_MEIPASS', os.path.dirname(os.path.abspath(__file__)))
            c1 = Path(base_path) / "_internal" / "FlashGBX"
            c2 = Path(base_path) / "FlashGBX"
            native_dir = c1 if (c1 / name).exists() else c2
        else:
            native_dir = Path(sysconfig.get_path("platlib")) / "FlashGBX"
        path = native_dir / name
        self._papi = ctypes.CDLL(str(path))
        self._load_ffi()
        atexit.register(self._unload_native_callbacks)

    def _load_ffi(self):
        self._papi.papi_fpga_program_sram.argtypes = [ctypes.c_char_p, ctypes.c_size_t, NATIVE_STRING_CALLBACK, NATIVE_PROGRESS_CALLBACK]
        self._papi.papi_fpga_program_sram.restype = ctypes.c_int

        self._papi.papi_fpga_reset.argtypes = []
        self._papi.papi_fpga_reset.restype = ctypes.c_int

        self._papi.papi_open.argtypes = [ctypes.c_uint16, ctypes.c_uint16]
        self._papi.papi_open.restype = ctypes.c_int

        self._papi.papi_is_open.argtypes = []
        self._papi.papi_is_open.restype = ctypes.c_int

        self._papi.papi_get_fw_info.argtypes = [ctypes.c_char_p, ctypes.c_uint16]
        self._papi.papi_get_fw_info.restype = ctypes.c_uint16

        self._papi.papi_close.argtypes = []
        self._papi.papi_close.restype = None

        self._papi.papi_send_to_lk.argtypes = [ctypes.c_void_p, ctypes.c_uint16]
        self._papi.papi_send_to_lk.restype = None

        self._papi.papi_send_to_lk_reset_output_buffer.argtypes = []
        self._papi.papi_send_to_lk_reset_output_buffer.restype = None

        self._papi.papi_send_to_lk_flush.argtypes = []
        self._papi.papi_send_to_lk_flush.restype = None

        self._papi.papi_recv_from_lk.argtypes = [ctypes.c_void_p, ctypes.c_uint16]
        self._papi.papi_recv_from_lk.restype = None

        self._papi.papi_recv_from_lk_reset_input_buffer.argtypes = []
        self._papi.papi_recv_from_lk_reset_input_buffer.restype = None

        self._papi.papi_recv_from_lk_pending_count.argtypes = []
        self._papi.papi_recv_from_lk_pending_count.restype = ctypes.c_uint16

        self._papi.papi_set_on_error_callback.argtypes = [NATIVE_STRING_CALLBACK]
        self._papi.papi_set_on_error_callback.restype = None

        def on_error_callback(ptr, count) -> None:
            self._on_native_error(bytes(ptr[:count]))
        self._lk_on_error_cb = NATIVE_STRING_CALLBACK(on_error_callback)
        self._papi.papi_set_on_error_callback(self._lk_on_error_cb)
        def on_debug_message_callback(ptr, count) -> None:
            self._on_native_debug_message(bytes(ptr[:count]))
        self._lk_on_debug_message_cb = NATIVE_STRING_CALLBACK(on_debug_message_callback)
        self._papi.papi_set_on_debug_message_callback(self._lk_on_debug_message_cb)

    def _unload_native_callbacks(self) -> None:
        self._papi.papi_set_on_debug_message_callback(NATIVE_STRING_CALLBACK(0))
        self._papi.papi_set_on_error_callback(NATIVE_STRING_CALLBACK(0))

    def _reg_ffi_recv_callback(self, reg_fn, py_fn):
        def cb(ptr, count) -> None:
            result = py_fn(count)
            to_copy = min(len(result), count)
            ctypes.memmove(ptr, result, to_copy)
        c_cb = NATIVE_STRING_CALLBACK(cb)
        reg_fn(c_cb)
        return c_cb

    def _on_native_error(self, data: bytes) -> None:
        message = data.decode('utf-8')
        dprint(f"{ANSI.RED}ERROR: {message}{ANSI.RESET}")
    def _on_native_debug_message(self, data: bytes) -> None:
        message = data.decode('utf-8')
        dprint(message)

    def _try_connect(self, port) -> MicrocodeDevice | None:
        if port is not None and port != self.PORT:
            return None
        dev = MicrocodeDevice(self._papi, self.USB_VENDOR_ID, self.USB_PRODUCT_ID)
        res = dev.open()
        # See papi_open() in PAPI.hpp
        match res:
            case 0: # Success
                pass
            case 1: # DeviceNotFound
                dprint(f"No {self.DEVICE_NAME} found")
                return None
            case 2: # InterfaceNotFound
                # Incorrect firmware version, handled in LoadFirmwareVersion
                pass
            case _:
                dprint(f"{ANSI.RED}ERROR: Failed to open device: {res}{ANSI.RESET}")
                return None

        old, self.DEVICE = self.DEVICE, dev
        match = self.LoadFirmwareVersion()
        self.DEVICE = old

        if not match:
            dev.close()
            return None

        challenge = os.urandom(1)[0]
        expected = (~challenge) & 0xFF
        dev.write(bytearray([self.DEVICE_CMD["PING"], challenge]))
        response = dev.read(1)
        if response != bytearray([expected]):
            dprint(f"{ANSI.RED}PING ERROR: expected 0x{challenge:02X} -> 0x{expected:02X}, got 0x{response.hex()}{ANSI.RESET}")
            dev.close()
            return None
        dprint(f"Initial ping OK: 0x{challenge:02X} -> 0x{expected:02X}")

        return dev


    def TryConnect(self, port, baudrate):
        dev = self._try_connect(port)
        if dev is None:
            return False
        dev.close()
        return True

    def Initialize(self, flashcarts, port=None, max_baud=2000000):
        if self.IsConnected(): self.DEVICE.close()

        dev = self._try_connect(port)
        if dev is None:
            self.DEVICE = None
            return False
        self.DEVICE = dev

        if not self.LoadFirmwareVersion():
            self.DEVICE = None
            dev.close()
            return False


        if self.FW is None or self.FW == {}:
            self.DEVICE = None
            dev.close()
            return False

        dprint(f"Found a {self.DEVICE_NAME}")
        dprint("Firmware information:", self.FW)

        conn_msg = []

        if self.DEVICE is None or not self.IsConnected():
            self.DEVICE = None
            if self.FW is not None:
                conn_msg.append([0, "Couldn’t communicate with the " + self.DEVICE_NAME + " device. Please disconnect and reconnect the device, then try again."])
            return False
        elif self.FW is None:
            dev.close()
            self.DEVICE = None
            return False
        elif "cfw_id" not in self.FW or self.FW["cfw_id"] != 'L': # Not a CFW by FredEmmott
            dprint("Incompatible firmware:", self.FW)
            dev.close()
            self.DEVICE = None
            return False

        self.DEVICE.timeout = self.DEVICE_TIMEOUT

        conn_msg.append([0, "No help is currently available when using a ModRetro Chromatic device"])

        # Load Flash Cartridge Handlers
        self.UpdateFlashCarts(flashcarts)

        # Stop after first found device

        return conn_msg

    # noinspection PyUnresolvedReferences
    def LoadFirmwareVersion(self):
        if self.DEVICE is None: return False
        if not hasattr(self.DEVICE, "_haveFredEmmottMicrocode"):
            dprint("Querying firmware version")

            match = self._query_firmware_version()
            if not match:
                self._program_sram()
                match = self._query_firmware_version()
            if not match:
                dprint("Failed to write firmware to SRAM")
                self.FW = None
                return False
            if match and not self._query_firmware_version():
                dprint("Firmware ID is unstable")
                self.FW = None
                return False
            if self.DEVICE is None:
                self.FW = None
                return False
            self.DEVICE._haveFredEmmottMicrocode = self._query_lk_firmware_version()
        return self.DEVICE._haveFredEmmottMicrocode


    def _query_firmware_version(self) -> bool:
        if self.DEVICE is None:
            return False
        try:
            self.DEVICE.reset_input_buffer()
            self.DEVICE.reset_output_buffer()

            cartio_firmware_id = self.DEVICE.get_fw_info()
            if len(cartio_firmware_id) == 0:
                dprint("0-byte response to firmware ID request; likely old version that expects USB-Serial handshake")
                return False

            view = memoryview(cartio_firmware_id)

            def consume(n: int):
                nonlocal view
                ret = view[:n]
                view = view[n:]
                return ret

            size = consume(1)[0]
            if size != len(cartio_firmware_id):
                dprint(f"Expected {len(cartio_firmware_id)} bytes, got {size}")
                return False

            if size < 8:
                dprint(f"Expected at least 8 bytes, got {size}")

            # BCD
            year = consume(2).hex()
            month = consume(1).hex()
            day = consume(1).hex()

            revision = consume(1)[0]

            upstream_major = consume(1)[0]
            upstream_minor = consume(1)[0]

            self.FW["device_name"] = self.DEVICE_NAME
            self.FW["pcb_name"] = self.DEVICE_NAME
            self.FW["fw_dt"] = f"{year}-{month}-{day}"
            self.FW["hw_Chromatic/fw_ver/CartIO"] = f"{year}.{month}.{day}.{revision}"
            self.FW["hw_Chromatic/fw_ver/Upstream"] = f"{upstream_major}.{upstream_minor}"

            if self.FW["hw_Chromatic/fw_ver/CartIO"] != self.REQUIRED_FW_VERSION:
                dprint(f"Running microcode firmware, but '{self.FW['hw_Chromatic/fw_ver/CartIO']}' is not a supported version (need v{self.REQUIRED_FW_VERSION})")
                return False
            dprint("Firmware matches")
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

    def _program_sram(self) -> bool:
        app = None
        orig_progress = None

        def message(s: str) -> None:
            print(s)
        def progress(value: int, max_value: int) -> None: pass

        if pyside:
            try:
                app = pyside.QtGui.QGuiApplication.instance()
                for window in pyside.QtGui.QGuiApplication.topLevelWindows():
                    widget = pyside.QtWidgets.QWidget.find(window.winId())
                    if hasattr(widget, "lblDevice"):
                        def gui_message(label, s:str) -> None:
                            label.setText(s)
                        message = lambda s, l = widget.lblDevice: gui_message(l, s)
                        orig_progress = widget.lblDevice.text()
                    if hasattr(widget, "SetProgressBars") and hasattr(widget, "prgStatus"):
                        progress = lambda value, max_value, w = widget: (w.SetProgressBars(0, max_value, value), w.prgStatus.repaint())
            except:
                pass

        def message_callback(ptr, count) -> None:
            raw = bytes(ptr[:count])
            s = str(raw, "utf-8")
            message(s)
            if app:
                app.processEvents()
        def progress_callback(value: int, max_value: int) -> None:
            progress(value, max_value)
            if app:
                app.processEvents()

        native_message = NATIVE_STRING_CALLBACK(message_callback)
        native_progress = NATIVE_PROGRESS_CALLBACK(progress_callback)

        try:
            self.DEVICE.close()

            zip_path = os.path.join(AppContext.APP_PATH, "res", "fw_Chromatic.zip")
            if not os.path.exists(zip_path):
                raise FileNotFoundError(f"File not found: {zip_path}")

            with zipfile.ZipFile(zip_path, "r") as zip_file:
                with zip_file.open("evt1_x2.fs") as f: fs_bytes = f.read()
            with tempfile.NamedTemporaryFile(suffix=".fs", delete_on_close=False) as fs_file:
                fs_file.write(fs_bytes)
                fs_file.close()
                path = fs_file.name.encode(locale.getencoding())
                self._papi.papi_fpga_program_sram(path, len(path), native_message, native_progress)

            begin = time.monotonic()
            while time.monotonic() - begin < 10:
                time.sleep(0.1)
                try:
                    self.DEVICE.open()
                    if self._query_firmware_version():
                        elapsed = time.monotonic() - begin
                        dprint(f"Programmed Chromatic SRAM in {elapsed} seconds")
                        return True
                    self.DEVICE.close()
                    if app:
                        app.processEvents()
                except SerialException:
                    continue
            return True
        except Exception as e:
            return False
        finally:
            progress(0, 100)
            if orig_progress:
                message(orig_progress)
                if app:
                    app.processEvents()

    def _query_lk_firmware_version(self) -> bool:
        self._write(self.DEVICE_CMD["QUERY_FW_INFO"])
        size = self._read(1)
        if size != 8: return False
        data = self._read(size)
        info = data[:8]
        keys = ["cfw_id", "fw_ver", "pcb_ver", "fw_ts"]
        values = struct.unpack(">cHBI", bytearray(info))
        self.FW.update(zip(keys, values))
        self.FW["cfw_id"] = self.FW["cfw_id"].decode('ascii')
        self.FW["fw_dt"] = datetime.datetime.fromtimestamp(self.FW["fw_ts"]).astimezone().replace(
            microsecond=0).isoformat()
        self.FW["ofw_ver"] = None
        self.FW["cart_power_ctrl"] = False
        self.FW["bootloader_reset"] = False

        size = self._read(1)
        name = self._read(size)
        if len(name) > 0:
            try:
                self.FW["pcb_name"] = name.decode("UTF-8").replace("\x00", "").strip()
            except:
                self.FW["pcb_name"] = self.DEVICE_NAME
        self.DEVICE_NAME = self.FW["pcb_name"]

        # Cartridge Power Control support
        temp = self._read(1)
        self.FW["cart_power_ctrl"] = True if temp & 1 == 1 else False
        self.FW["cart_presence_switch"] = True if (temp >> 1) & 1 == 1 else False
        self.FW["cart_mode_switch"] = True if (temp >> 2) & 1 == 1 else False

        # Reset to bootloader support
        self.FW["bootloader_reset"] = True if self._read(1) == 1 else False
        return True

    def ChangeBaudRate(self, _):
        dprint("Baudrate change is not supported.")

    def GetFirmwareVersion(self, more=False):
        return f"L{self.FW['fw_ver']} / MC v{self.FW["hw_Chromatic/fw_ver/CartIO"]} / ModRetro v{self.FW['hw_Chromatic/fw_ver/Upstream']}"

    def GetFullNameExtended(self, more=False):
        return f"{self.GetFullName()} - {self.GetFirmwareVersion()}"

    def GetFullName(self):
        # Superclass behavior includes PCB version, which isn't applicable here
        return self.GetName()

    def CanSetVoltageBySwitch(self):
        return False

    def CanSetVoltageByAutoswitch(self):
        return False

    def CanSetVoltageByCode(self):
        return False

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
            self.DEVICE.close()
        self.DEVICE = None
        self.MODE = None
