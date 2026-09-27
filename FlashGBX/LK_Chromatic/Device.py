import ctypes

class Device:
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
        return self._papi.papi_open(self._vendorID, self._productID)

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