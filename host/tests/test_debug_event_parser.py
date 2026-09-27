import struct
import zlib
from pathlib import Path

import pytest

from host.tools.capture_debug_event import EventFormatError, parse_event, parse_ev01


FIXTURE_EVENTS = Path(__file__).parent / "fixtures" / "events"


def make_golden_event() -> bytes:
    payload = struct.pack("<hhhhhhHbB", 1, -2, 3, -4, 5, -6, 0x1234, -7, 0xA5)
    payload += struct.pack("<hhhhhhHbB", 0x1122, 0x3344, 0x5566,
                           -0x1122, -0x3344, -0x5566, 0xBEEF, 8, 0x5A)
    header = struct.pack(
        "<4sHHIQIIIIHHIIII8s",
        b"EV01", 1, 64, 0x11223344, 123456, 1600, 0x55667788,
        0, 2, 2, 1, 0x01020304, 100, len(payload), zlib.crc32(payload), b"\0" * 8,
    )
    return header + payload


def test_parses_the_ev01_golden_vector():
    event = parse_ev01(make_golden_event())

    assert event.event_id == 0x11223344
    assert event.sample_rate_hz == 1600
    assert event.posttrigger_samples == 2
    assert event.samples[0] == (1, -2, 3, -4, 5, -6, 0x1234, -7, 0xA5)


def test_dispatches_an_ev02_event_with_frozen_context():
    payload = struct.pack("<hhhhhhHbB", 1, -2, 3, -4, 5, -6, 0x1234, -7, 0xA5)
    header = bytearray(128)
    struct.pack_into("<4sHHI", header, 0, b"EV02", 2, 128, 7)
    struct.pack_into("<QqI", header, 12, 999, 1710000000, 3)
    struct.pack_into("<IIII", header, 32, 1600, 11, 0, 1)
    struct.pack_into("<HHIIII", header, 48, 0, 0, 12, 34,
                     len(payload), zlib.crc32(payload))
    struct.pack_into("<BBH", header, 68, 2, 0, 0b111)
    struct.pack_into("<IhHI", header, 72, 0x12, 2500, 1, 56500)

    event = parse_event(bytes(header) + payload)

    assert event.event_id == 7
    assert event.version == 2
    assert event.utc_unix_seconds == 1710000000
    assert event.temperature_centi_c == 2500
    assert event.humidity_milli_rh == 56500


def test_dispatches_an_ev03_event_with_loss_evidence():
    event = parse_event((FIXTURE_EVENTS / "valid-v3-loss-43.terp-event").read_bytes())

    assert event.version == 3
    assert event.lost_sample_count == 6
    assert event.first_lost_sequence == 102
    assert event.last_lost_sequence == 109
    assert event.loss_episode_count == 2


@pytest.mark.parametrize("mutate", [
    lambda data: b"BAD!" + data[4:],
    lambda data: data[:48] + (33).to_bytes(4, "little") + data[52:],
    lambda data: data[:-1] + bytes([data[-1] ^ 0x01]),
])
def test_rejects_magic_length_and_crc_corruption(mutate):
    with pytest.raises(EventFormatError):
        parse_ev01(mutate(make_golden_event()))
