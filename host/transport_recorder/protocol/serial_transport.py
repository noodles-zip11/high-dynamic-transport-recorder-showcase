"""pyserial adapter for TERP byte transports."""

from __future__ import annotations

from typing import Any

import serial
from serial.tools import list_ports

from .client import TransportDisconnected


class SerialTransport:
    def __init__(self, serial_port: Any) -> None:
        self._serial_port = serial_port

    @classmethod
    def open(cls, port: str, baudrate: int = 115200,
             timeout_seconds: float = 0.2) -> "SerialTransport":
        try:
            serial_port = serial.Serial(
                port=port,
                baudrate=baudrate,
                timeout=timeout_seconds,
                write_timeout=timeout_seconds,
                dsrdtr=False,
                rtscts=False,
                xonxoff=False,
            )
            serial_port.dtr = False
            serial_port.rts = False
            serial_port.reset_input_buffer()
            return cls(serial_port)
        except serial.SerialException as error:
            raise TransportDisconnected(str(error)) from error

    def write(self, data: bytes) -> None:
        try:
            if not self._serial_port.is_open:
                raise TransportDisconnected("serial port is closed")
            written = self._serial_port.write(data)
            if written != len(data):
                raise TransportDisconnected("short serial write")
        except serial.SerialException as error:
            raise TransportDisconnected(str(error)) from error

    def read(self, maximum_bytes: int) -> bytes:
        try:
            if not self._serial_port.is_open:
                raise TransportDisconnected("serial port is closed")
            return bytes(self._serial_port.read(maximum_bytes))
        except serial.SerialException as error:
            raise TransportDisconnected(str(error)) from error

    def close(self) -> None:
        if self._serial_port.is_open:
            self._serial_port.close()


def list_serial_ports() -> list[dict[str, str | None]]:
    return [
        {
            "device": port.device,
            "description": port.description,
            "serial_number": port.serial_number,
        }
        for port in sorted(list_ports.comports(), key=lambda item: item.device)
    ]
