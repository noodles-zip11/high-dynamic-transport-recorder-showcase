"""Validate and persist EV01 debug-event files produced by the firmware."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
import struct
import zlib


EV01_MAGIC = b"EV01"
EV01_VERSION = 1
EV01_HEADER_SIZE = 64
EV01_SAMPLE_SIZE = 16
EV02_MAGIC = b"EV02"
EV02_VERSION = 2
EV02_HEADER_SIZE = 128
EV03_MAGIC = b"EV03"
EV03_VERSION = 3
EV03_HEADER_SIZE = 160
_HEADER = struct.Struct("<4sHHIQIIIIHHIIII8s")
_SAMPLE = struct.Struct("<hhhhhhHbB")


class EventFormatError(ValueError):
    """The bytes do not form one complete, integrity-checked EV01 event."""


@dataclass(frozen=True)
class DebugEvent:
    event_id: int
    trigger_monotonic_tick: int
    sample_rate_hz: int
    trigger_sequence: int
    pretrigger_samples: int
    posttrigger_samples: int
    subtrigger_count: int
    flags: int
    peak_magnitude_sq: int
    threshold_magnitude_sq: int
    samples: tuple[tuple[int, int, int, int, int, int, int, int, int], ...]
    version: int = EV01_VERSION
    utc_unix_seconds: int | None = None
    temperature_centi_c: int | None = None
    humidity_milli_rh: int | None = None
    lost_sample_count: int = 0
    first_lost_sequence: int = 0
    last_lost_sequence: int = 0
    loss_episode_count: int = 0
    first_loss_monotonic_us: int = 0
    last_loss_monotonic_us: int = 0


def parse_ev01(data: bytes) -> DebugEvent:
    """Parse EV01 only after checking its complete length and CRC32."""
    if len(data) < EV01_HEADER_SIZE:
        raise EventFormatError("EV01 header is incomplete")

    (magic, version, header_size, event_id, trigger_tick, sample_rate, trigger_sequence,
     pretrigger_samples, posttrigger_samples, subtrigger_count, flags, peak_magnitude_sq,
     threshold_magnitude_sq, payload_length, payload_crc32, _) = _HEADER.unpack_from(data)
    if magic != EV01_MAGIC:
        raise EventFormatError("unexpected EV01 magic")
    if version != EV01_VERSION or header_size != EV01_HEADER_SIZE:
        raise EventFormatError("unsupported EV01 version or header size")
    if payload_length != len(data) - EV01_HEADER_SIZE:
        raise EventFormatError("EV01 payload length does not match file length")
    expected_payload_length = (pretrigger_samples + posttrigger_samples) * EV01_SAMPLE_SIZE
    if payload_length != expected_payload_length:
        raise EventFormatError("EV01 sample counts do not match payload length")

    payload = data[EV01_HEADER_SIZE:]
    if zlib.crc32(payload) != payload_crc32:
        raise EventFormatError("EV01 payload CRC32 mismatch")

    samples = tuple(_SAMPLE.unpack_from(payload, offset)
                    for offset in range(0, payload_length, EV01_SAMPLE_SIZE))
    return DebugEvent(
        event_id=event_id,
        trigger_monotonic_tick=trigger_tick,
        sample_rate_hz=sample_rate,
        trigger_sequence=trigger_sequence,
        pretrigger_samples=pretrigger_samples,
        posttrigger_samples=posttrigger_samples,
        subtrigger_count=subtrigger_count,
        flags=flags,
        peak_magnitude_sq=peak_magnitude_sq,
        threshold_magnitude_sq=threshold_magnitude_sq,
        samples=samples,
    )


def _parse_extended_event(
    data: bytes,
    *,
    expected_magic: bytes,
    expected_version: int,
    expected_header_size: int,
) -> DebugEvent:
    if len(data) < expected_header_size:
        raise EventFormatError(f"EV0{expected_version} header is incomplete")
    magic, version, header_size, event_id = struct.unpack_from("<4sHHI", data, 0)
    if (
        magic != expected_magic
        or version != expected_version
        or header_size != expected_header_size
    ):
        raise EventFormatError(f"unsupported EV0{expected_version} version or header size")
    trigger_tick, utc_seconds, _epoch = struct.unpack_from("<QqI", data, 12)
    sample_rate, trigger_sequence, pretrigger_samples, posttrigger_samples = struct.unpack_from(
        "<IIII", data, 32
    )
    subtrigger_count, flags = struct.unpack_from("<HH", data, 48)
    peak_magnitude_sq, threshold_magnitude_sq, payload_length, payload_crc32 = struct.unpack_from(
        "<IIII", data, 52
    )
    _health, _power, context_flags = struct.unpack_from("<BBH", data, 68)
    _reset, temperature_centi_c, _environment_age, humidity_milli_rh = struct.unpack_from(
        "<IhHI", data, 72
    )
    if payload_length != len(data) - expected_header_size:
        raise EventFormatError(f"EV0{expected_version} payload length does not match file length")
    if payload_length != (pretrigger_samples + posttrigger_samples) * EV01_SAMPLE_SIZE:
        raise EventFormatError(f"EV0{expected_version} sample counts do not match payload length")
    payload = data[expected_header_size:]
    if zlib.crc32(payload) != payload_crc32:
        raise EventFormatError(f"EV0{expected_version} payload CRC32 mismatch")
    samples = tuple(_SAMPLE.unpack_from(payload, offset)
                    for offset in range(0, payload_length, EV01_SAMPLE_SIZE))
    return DebugEvent(
        event_id=event_id,
        trigger_monotonic_tick=trigger_tick,
        sample_rate_hz=sample_rate,
        trigger_sequence=trigger_sequence,
        pretrigger_samples=pretrigger_samples,
        posttrigger_samples=posttrigger_samples,
        subtrigger_count=subtrigger_count,
        flags=flags,
        peak_magnitude_sq=peak_magnitude_sq,
        threshold_magnitude_sq=threshold_magnitude_sq,
        samples=samples,
        version=expected_version,
        utc_unix_seconds=utc_seconds if context_flags & 1 else None,
        temperature_centi_c=temperature_centi_c if context_flags & 2 else None,
        humidity_milli_rh=humidity_milli_rh if context_flags & 2 else None,
        lost_sample_count=struct.unpack_from("<I", data, 128)[0]
        if expected_version == EV03_VERSION else 0,
        first_lost_sequence=struct.unpack_from("<I", data, 132)[0]
        if expected_version == EV03_VERSION else 0,
        last_lost_sequence=struct.unpack_from("<I", data, 136)[0]
        if expected_version == EV03_VERSION else 0,
        loss_episode_count=struct.unpack_from("<I", data, 140)[0]
        if expected_version == EV03_VERSION else 0,
        first_loss_monotonic_us=struct.unpack_from("<Q", data, 144)[0]
        if expected_version == EV03_VERSION else 0,
        last_loss_monotonic_us=struct.unpack_from("<Q", data, 152)[0]
        if expected_version == EV03_VERSION else 0,
    )


def parse_ev02(data: bytes) -> DebugEvent:
    """Parse EV02 with frozen time/environment context and payload CRC32."""
    return _parse_extended_event(
        data,
        expected_magic=EV02_MAGIC,
        expected_version=EV02_VERSION,
        expected_header_size=EV02_HEADER_SIZE,
    )


def parse_ev03(data: bytes) -> DebugEvent:
    """Parse EV03 with frozen context, CRC32 and data-loss evidence."""
    return _parse_extended_event(
        data,
        expected_magic=EV03_MAGIC,
        expected_version=EV03_VERSION,
        expected_header_size=EV03_HEADER_SIZE,
    )


def parse_event(data: bytes) -> DebugEvent:
    """Dispatch an integrity-checked EV01, EV02 or EV03 event."""
    if len(data) < 4:
        raise EventFormatError("event magic is incomplete")
    if data[:4] == EV01_MAGIC:
        return parse_ev01(data)
    if data[:4] == EV02_MAGIC:
        return parse_ev02(data)
    if data[:4] == EV03_MAGIC:
        return parse_ev03(data)
    raise EventFormatError("unexpected event magic")


def save_valid_ev01(data: bytes, output_path: Path) -> DebugEvent:
    """Validate first, then persist the exact received EV01 bytes."""
    event = parse_ev01(data)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_bytes(data)
    return event
