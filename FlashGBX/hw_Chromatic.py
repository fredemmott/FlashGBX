# -*- coding: utf-8 -*-
# FlashGBX
# Author: Lesserkuma (github.com/Lesserkuma)
# Author: Fred Emmott
import locale
import sys
from pathlib import Path
import sysconfig
import ctypes
import tempfile
import zipfile

from . import pyside

# pylint: disable=wildcard-import, unused-wildcard-import
from .LK_Device import *
from .LK_Chromatic import Device as MicrocodeDevice

from typing import cast

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

class GbxDevice(LK_Device):
    DEVICE_NAME = "Chromatic"
    ID_PREFIX = b"fredemmott/CartIO\x00"

    USB_VENDOR_ID = 0x374e
    USB_PRODUCT_ID = 0x0101

    MAX_BUFFER_READ = 4096
    # Also limited by the TX buffer, for VerifyData calls
    MAX_BUFFER_WRITE = 4096

    _c_callbacks = []

    def __init__(self):
        super().__init__()
        self._load_lk()

    def _load_lk(self):
        native_dir = Path(sysconfig.get_path("platlib")) / "FlashGBX"
        match sys.platform:
            case "win32":
                os.add_dll_directory(os.path.dirname(native_dir))
                ext = ".dll"
            case _:
                # even on Darwin (macOS), we get a .so, not a .dylib
                ext = ".so"
        lk_path =  native_dir / f"_LK_Chromatic{ext}"
        self._papi = ctypes.CDLL(str(lk_path))
        self._load_ffi()

    def _load_ffi(self):
        self._papi.papi_fpga_program_sram.argtypes = [ctypes.c_char_p, ctypes.c_size_t, NATIVE_STRING_CALLBACK, NATIVE_PROGRESS_CALLBACK]
        self._papi.papi_fpga_program_sram.restype = ctypes.c_int

        self._papi.papi_fpga_reset.argtypes = []
        self._papi.papi_fpga_reset.restype = ctypes.c_int

        self._papi.papi_open.argtypes = [ctypes.c_uint16, ctypes.c_uint16, ctypes.c_uint8]
        self._papi.papi_open.restype = None

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

        def cb(ptr, count) -> None:
            self._lk_on_error(bytes(ptr[:count]))
        self._lk_on_error_cb = NATIVE_STRING_CALLBACK(cb)
        self._papi.papi_set_on_error_callback(self._lk_on_error_cb)

    def _reg_ffi_recv_callback(self, reg_fn, py_fn):
        def cb(ptr, count) -> None:
            result = py_fn(count)
            to_copy = min(len(result), count)
            ctypes.memmove(ptr, result, to_copy)
        c_cb = NATIVE_STRING_CALLBACK(cb)
        reg_fn(c_cb)
        return c_cb

    def _lk_on_error(self, data: bytes) -> None:
        self.DEVICE.lk_on_error(data)
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
                if comports[i].vid == self.USB_VENDOR_ID and comports[i].pid == self.USB_PRODUCT_ID:
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
        if not hasattr(self.DEVICE, "_haveFredEmmottMicrocode"):
            match = self._query_firmware_version()
            if not match:
                self._program_sram()
                match = self._query_firmware_version()
            if not match:
                dprint("Failed to write firmware to SRAM")
                self.FW = None
            if match and not self._query_firmware_version():
                dprint("Firmware ID is unstable")
                self.FW = None
            if self.DEVICE is None:
                return False
            self._activate_cartridge_io_mode()
            self.DEVICE._haveFredEmmottMicrocode = True
        return self.DEVICE._haveFredEmmottMicrocode


    def _query_firmware_version(self) -> bool:
        try:
            self.DEVICE.timeout = 0.075
            self.DEVICE.reset_input_buffer()
            self.DEVICE.reset_output_buffer()

            self._write(bytearray(b'\xAA\x55\x90'))
            time.sleep(0.01)
            device_id = self.DEVICE.read(self.DEVICE.in_waiting)

            view = memoryview(device_id)

            p = 0
            def consume(n: int):
                nonlocal view
                ret = view[:n]
                view = view[n:]
                return ret

            match = False
            while len(view) > 2:
                section_len = int.from_bytes(consume(2), "big", signed=False)
                if len(view) < (section_len - 2): return False
                if consume(len(self.ID_PREFIX)) != self.ID_PREFIX: continue
                match = True
                break

            if not match: return False

            # BCD
            year = consume(2).hex()
            month = consume(1).hex()
            day = consume(1).hex()

            revision = consume(1)[0]

            upstream_major = consume(1)[0]
            upstream_minor = consume(1)[0]
            usb_interface = consume(1)[0]

            self.FW["device_name"] = self.DEVICE_NAME
            self.FW["pcb_name"] = self.DEVICE_NAME
            self.FW["fw_dt"] = f"{year}-{month}-{day}"
            self.FW["hw_Chromatic/fw_ver/CartIO"] = f"{year}.{month}.{day}.{revision}"
            self.FW["hw_Chromatic/fw_ver/Upstream"] = f"{upstream_major}.{upstream_minor}"
            self.FW["hw_Chromatic/CartIO_usb_if"] = usb_interface

            if self.FW["hw_Chromatic/fw_ver/CartIO"] != "2026.09.13.0":
                dprint("Running microcode firmware, but not a supported version")
                return False
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

    def _activate_cartridge_io_mode(self):
        self._write(bytearray(b'CartIO\0')) # Switch mode
        time.sleep(0.10)

        self.DEVICE.__class__ = MicrocodeDevice
        cast(MicrocodeDevice, self.DEVICE).init_chromatic(self._papi)

        self._papi.papi_open(self.USB_VENDOR_ID, self.USB_PRODUCT_ID, self.FW["hw_Chromatic/CartIO_usb_if"])

        self._query_lk_firmware_version()

    def _program_sram(self) -> bool:
        app = None
        orig_progress = None

        def message(s: str) -> None: pass
        def progress(value: int, max_value: int) -> None: pass

        try:
            app = pyside.QtGui.QGuiApplication.instance()
            for window in pyside.QtGui.QGuiApplication.topLevelWindows():
                widget = pyside.QtWidgets.QWidget.find(window.winId())
                if hasattr(widget, "lblDevice"):
                    def gui_progress(label, s:str) -> None:
                        label.setText(s)
                    message = lambda s, l = widget.lblDevice: gui_progress(l, s)
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

        native_message = NATIVE_STRING_CALLBACK(message_callback)
        native_progress = NATIVE_PROGRESS_CALLBACK(progress)

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

    def _query_lk_firmware_version(self):
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
        return True

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
