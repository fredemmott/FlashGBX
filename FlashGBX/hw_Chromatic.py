# -*- coding: utf-8 -*-
# FlashGBX
# Author: Lesserkuma (github.com/Lesserkuma)
# Author: Fred Emmott
import sys
from pathlib import Path
import sysconfig
import ctypes

# pylint: disable=wildcard-import, unused-wildcard-import
from .LK_Device import *
from .LK_Chromatic import Device as MicrocodeDevice

from typing import cast

NATIVE_DATA_CALLBACK = ctypes.CFUNCTYPE(
    None,  # return void
    ctypes.POINTER(ctypes.c_uint8),  # uint8_t* data
    ctypes.c_uint16  # uint16_t len
)

class GbxDevice(LK_Device):
    DEVICE_NAME = "Chromatic"
    LK_FW_VERSION = 15

    USB_VENDOR_ID = 0x374e
    USB_PRODUCT_ID = 0x0101

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
            case "darwin":
                ext = ".dylib"
            case _:
                ext = ".so"
        lk_path =  native_dir / f"_LK_Chromatic{ext}"
        self._lk = ctypes.CDLL(str(lk_path))
        self._load_ffi()

    def _load_ffi(self):
        self._lk.papi_open.argtypes = [ctypes.c_uint16, ctypes.c_uint16, ctypes.c_uint8]
        self._lk.papi_open.restype = None

        self._lk.papi_send_to_lk.argtypes = [ctypes.c_void_p, ctypes.c_uint16]
        self._lk.papi_send_to_lk.restype = None

        self._lk.papi_recv_from_lk.argtypes = [ctypes.c_void_p, ctypes.c_uint16]
        self._lk.papi_recv_from_lk.restype = None

        self._lk.papi_set_on_error_callback.argtypes = [NATIVE_DATA_CALLBACK]
        self._lk.papi_set_on_error_callback.restype = None

        def cb(ptr, count) -> None:
            self._lk_on_error(bytes(ptr[:count]))
        self._lk_on_error_cb = NATIVE_DATA_CALLBACK(cb)
        self._lk.papi_set_on_error_callback(self._lk_on_error_cb)

    def _reg_ffi_recv_callback(self, reg_fn, py_fn):
        def cb(ptr, count) -> None:
            result = py_fn(count)
            to_copy = min(len(result), count)
            ctypes.memmove(ptr, result, to_copy)
        c_cb = NATIVE_DATA_CALLBACK(cb)
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

            if device_id[0:5] == b"Micro":
                dprint("Running custom microcode firmware; was never supported")
                self.FW = None
                return False

            if not device_id.startswith(b"fredemmott/FlashGBX\x00"):
                dprint("Not running fredemmott/FlashGBX firmware")
                self.FW = None
                return False


            if len(device_id) != 28:
                dprint("Running supported firmware, but not a supported version")
                self.FW = None
                return False

            # BCD
            year = device_id[20:22].hex()
            month = device_id[22:23].hex()
            day = device_id[23:24].hex()

            revision = device_id[24]

            upstream_major = device_id[25]
            upstream_minor = device_id[26]
            usb_interface = device_id[27]

            self.FW["fw_dt"] = f"{year}-{month}-{day}"
            self.FW["fw_ver/ChromaticDumper"] = f"{year}.{month}.{day}.{revision}"
            self.FW["fw_ver/Upstream"] = f"{upstream_major}.{upstream_minor}"

            if self.FW["fw_ver/ChromaticDumper"] != "2026.08.27.0":
                dprint("Running microcode firmware, but not a supported version")
                self.FW = None
                return False

            self.FW["cfw_id"] = "L"
            self.FW["fw_ver"] = self.LK_FW_VERSION

            self.FW["pcb_ver"] = None
            self.FW["ofw_ver"] = None
            self.FW["pcb_name"] = "GWA5-25A"


            # Doesn't appear to be physically supported
            self.FW["cart_power_ctrl"] = False
            self.FW["bootloader_reset"] = False

            self._write(bytearray(b'\x55\xAA'))
            time.sleep(0.01)
            device_id_dup = self.DEVICE.read(self.DEVICE.in_waiting)
            if device_id_dup != device_id:
                raise Exception("Device ID mismatch")

            self._write(bytearray(b'LK')) # Switch mode
            time.sleep(0.10)
            junk = self.DEVICE.read(self.DEVICE.in_waiting)

            self.DEVICE.__class__ = MicrocodeDevice
            cast(MicrocodeDevice, self.DEVICE).init_chromatic(self._lk.papi_recv_from_lk, self._lk.papi_send_to_lk)

            self._lk.papi_open(self.USB_VENDOR_ID, self.USB_PRODUCT_ID, usb_interface)

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

    def GetFirmwareVersion(self, more=False):
        return f"v{self.FW["fw_ver/ChromaticDumper"]} (based on v{self.FW['fw_ver/Upstream']})"

    def GetFullNameExtended(self, more=False):
        return "{:s} – Firmware {:s} ({:s})".format(self.GetFullName(), self.GetFirmwareVersion(), self.GetPort())

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
            dprint("Disconnecting from the device")
            self.DEVICE.close()
        self.DEVICE = None
        self.MODE = None
