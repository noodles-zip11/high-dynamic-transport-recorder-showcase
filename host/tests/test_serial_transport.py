from host.transport_recorder.protocol import serial_transport


def test_open_disables_modem_control_lines_and_clears_stale_input(monkeypatch) -> None:
    created: dict[str, object] = {}

    class FakeSerial:
        def __init__(self, **kwargs: object) -> None:
            created.update(kwargs)
            self.is_open = True
            self.dtr = True
            self.rts = True
            self.reset_input_buffer_calls = 0

        def reset_input_buffer(self) -> None:
            self.reset_input_buffer_calls += 1

        def close(self) -> None:
            self.is_open = False

    monkeypatch.setattr(serial_transport.serial, "Serial", FakeSerial)

    transport = serial_transport.SerialTransport.open("COM8")

    assert created["dsrdtr"] is False
    assert created["rtscts"] is False
    assert created["xonxoff"] is False
    serial_port = transport._serial_port
    assert serial_port.dtr is False
    assert serial_port.rts is False
    assert serial_port.reset_input_buffer_calls == 1
