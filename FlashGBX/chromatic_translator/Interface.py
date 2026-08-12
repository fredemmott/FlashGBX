from abc import abstractmethod
from typing import Protocol, Collection, Tuple

class Interface(Protocol):
    @abstractmethod
    def lk_response(self, data: bytes):
        raise NotImplementedError()

    @abstractmethod
    def mc_disable_cart(self):
        raise NotImplementedError()

    @abstractmethod
    def mc_set_variables(self, fw_vars: dict[int, int]):
        raise NotImplementedError()


    # Add cart requests to the queue
    #
    # `flush` supports up to 255 commands; for more than 255, use `mc_exec_enqueued`
    @abstractmethod
    def mc_enqueue(
            self,
            reqs: Collection[Tuple[int, int]],
            flush: bool = True,
            is_write: bool = False,
            is_flash: bool = False,
            wait_for_status: bool = False) -> None:
        raise NotImplementedError()

    # Flush an arbitrary number of requests, pipelining as appropriate, and returning
    # the corresponding data
    @abstractmethod
    def mc_exec_enqueued(self) -> bytes:
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
    def mc_ping(self, challenge: int) -> int:
        raise NotImplementedError()

    @abstractmethod
    def mc_wait_for_ack(self) -> None:
        raise NotImplementedError()

