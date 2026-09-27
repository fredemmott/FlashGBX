import ctypes

import serial

from FlashGBX.Logging import dprint


class Device:
    STATUS_SUCCESS = 0
    ERROR_DEVICE_NOT_FOUND = 1
    ERROR_INTERFACE_NOT_FOUND = 2

    def __init__(self, papi, vendor_id: int, product_id: int):
        self._papi = papi
        self._vendorID = vendor_id
        self._productID = product_id

    def flush(self):
        self._papi.papi_send_to_lk_flush()
        pass

    def init_chromatic(self, papi):
        self._papi = papi

    def open(self) -> int:
        # See papi_open() in PAPI.hpp for return codes
        serial_ports = serial.tools.list_ports.comports()
        have_serial = any(p.vid == self._vendorID and p.pid == self._productID for p in serial_ports)
        if not have_serial:
            dprint(
                f"No matching USB serial device found - looked for VID {self._vendorID:04x} and PID {self._productID:04x}")
            return self.ERROR_DEVICE_NOT_FOUND
        res = self._papi.papi_open(self._vendorID, self._productID)
        if res == self.ERROR_DEVICE_NOT_FOUND:
            dprint("Found matching USB serial device but no libusb device, assuming incompatible firmware")
            return self.ERROR_INTERFACE_NOT_FOUND
        return res

    def close(self):
        self._papi.papi_close()

    def read(self, size = 1) -> bytearray:
        buf = ctypes.create_string_buffer(size)
        self._papi.papi_recv_from_lk(buf, size)
        return bytearray(buf)

    def get_fw_info(self) -> bytearray:
        buf = ctypes.create_string_buffer(255)
        size = self._papi.papi_get_fw_info(buf, 255)
        return bytearray(buf[:size])

    def write(self, data):
        if not isinstance(data, (bytes, bytearray)):
            raise NotImplementedError()
        data = bytearray(data)
        count = len(data)
        buf_type = ctypes.c_char * count
        buf = buf_type.from_buffer(data)

        self._papi.papi_send_to_lk(ctypes.byref(buf), count)
        return len(data)

    @property
    def in_waiting(self):
        return self._papi.papi_recv_from_lk_pending_count()

    def reset_input_buffer(self):
        self._papi.papi_recv_from_lk_reset_input_buffer()

    def reset_output_buffer(self):
        self._papi.papi_send_to_lk_reset_output_buffer()

    # Legacy but used in LK_Device
    def isOpen(self) -> bool:
        return self.is_open()

    def is_open(self) -> bool:
        return self._papi.papi_is_open()