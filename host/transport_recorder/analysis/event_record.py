"""Strict decoder for EV02 and EV03 event records downloaded through TERP."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
import struct
from typing import Final
import zlib

import numpy as np


EV02_MAGIC: Final = b"EV02"
EV02_VERSION: Final = 2
EV02_HEADER_BYTES: Final = 128
EV03_MAGIC: Final = b"EV03"
EV03_VERSION: Final = 3
EV03_HEADER_BYTES: Final = 160
SAMPLE_BYTES: Final = 16
CONTEXT_VALID_MASK: Final = 0x007F
EVENT_FLAG_DATA_LOSS: Final = 1 << 3

_SAMPLE_DTYPE: Final = np.dtype(
    [
        ("accel_x", "<i2"),
        ("accel_y", "<i2"),
        ("accel_z", "<i2"),
        ("gyro_x", "<i2"),
        ("gyro_y", "<i2"),
        ("gyro_z", "<i2"),
        ("sensor_timestamp", "<u2"),
        ("temperature", "i1"),
        ("fifo_header", "u1"),
    ]
)


class EventFormatError(ValueError):
    """The event is not complete, supported or internally consistent."""


@dataclass(frozen=True)
class EventMetadata:
    format_version: int
    event_id: int
    trigger_monotonic_us: int
    utc_unix_seconds: int
    time_epoch_id: int
    sample_rate_hz: int
    trigger_sequence: int
    pretrigger_samples: int
    posttrigger_samples: int
    subtrigger_count: int
    flags: int
    peak_magnitude_sq: int
    threshold_magnitude_sq: int
    payload_crc32: int
    health_state: int
    power_state: int
    context_valid_flags: int
    reset_raw_flags: int
    temperature_centi_c: int
    environment_age_seconds: int
    humidity_milli_rh: int
    free_log_bytes: int
    sample_pool_min_free: int
    last_fault_code: int
    imu_transport_error_count: int
    imu_dma_error_count: int
    sample_pool_backpressure_count: int
    storage_error_count: int
    event_export_error_count: int
    transition_sequence: int
    lost_sample_count: int
    first_lost_sequence: int
    last_lost_sequence: int
    loss_episode_count: int
    first_loss_monotonic_us: int
    last_loss_monotonic_us: int

    @property
    def utc_valid(self) -> bool:
        return bool(self.context_valid_flags & 0x0001)


@dataclass(frozen=True)
class DecodedEvent:
    metadata: EventMetadata
    accel_counts: np.ndarray
    gyro_counts: np.ndarray
    sensor_timestamps: np.ndarray
    sample_temperatures_c: np.ndarray
    fifo_headers: np.ndarray
    timestamps_us: np.ndarray
    accel_magnitude_counts: np.ndarray

    @property
    def sample_count(self) -> int:
        return int(self.accel_counts.shape[0])

    @property
    def trigger_index(self) -> int:
        return self.metadata.pretrigger_samples


def load_event(path: Path) -> DecodedEvent:
    """Read and validate one complete supported event before exposing samples."""

    raw = path.read_bytes()
    if len(raw) < 8:
        raise EventFormatError("record length is shorter than event prefix")

    magic = raw[:4]
    version, header_bytes = struct.unpack_from("<HH", raw, 4)
    if magic == EV02_MAGIC:
        expected_version = EV02_VERSION
        expected_header_bytes = EV02_HEADER_BYTES
    elif magic == EV03_MAGIC:
        expected_version = EV03_VERSION
        expected_header_bytes = EV03_HEADER_BYTES
    else:
        raise EventFormatError("unsupported event magic")
    if version != expected_version:
        raise EventFormatError(f"unsupported event version {version}")
    if header_bytes != expected_header_bytes:
        raise EventFormatError(f"unsupported event header length {header_bytes}")
    if len(raw) < header_bytes:
        raise EventFormatError("record length is shorter than event header")

    metadata, payload_length = _decode_metadata(raw, version)
    expected_samples = metadata.pretrigger_samples + metadata.posttrigger_samples
    if payload_length != expected_samples * SAMPLE_BYTES:
        raise EventFormatError("payload length does not match sample counts")
    if len(raw) != header_bytes + payload_length:
        raise EventFormatError("record length does not match event payload")

    payload = raw[header_bytes:]
    actual_crc32 = zlib.crc32(payload) & 0xFFFFFFFF
    if actual_crc32 != metadata.payload_crc32:
        raise EventFormatError("payload CRC mismatch")

    samples = np.frombuffer(payload, dtype=_SAMPLE_DTYPE, count=expected_samples)
    accel_counts = np.column_stack(
        (samples["accel_x"], samples["accel_y"], samples["accel_z"])
    ).astype(np.int16, copy=False)
    gyro_counts = np.column_stack(
        (samples["gyro_x"], samples["gyro_y"], samples["gyro_z"])
    ).astype(np.int16, copy=False)
    timestamps_us = (
        np.arange(expected_samples, dtype=np.int64) * 1_000_000
    ) // metadata.sample_rate_hz
    magnitude = np.sqrt(
        np.sum(accel_counts.astype(np.float64) ** 2, axis=1, dtype=np.float64)
    )

    event = DecodedEvent(
        metadata=metadata,
        accel_counts=accel_counts,
        gyro_counts=gyro_counts,
        sensor_timestamps=samples["sensor_timestamp"].copy(),
        sample_temperatures_c=samples["temperature"].copy(),
        fifo_headers=samples["fifo_header"].copy(),
        timestamps_us=timestamps_us,
        accel_magnitude_counts=magnitude,
    )
    _freeze_event_arrays(event)
    return event


def _decode_metadata(raw: bytes, version: int) -> tuple[EventMetadata, int]:
    context_valid_flags = _u16(raw, 70)
    if context_valid_flags & ~CONTEXT_VALID_MASK:
        raise EventFormatError("context validity flags use reserved bits")

    payload_length = _u32(raw, 60)
    metadata = EventMetadata(
        format_version=_u16(raw, 4),
        event_id=_u32(raw, 8),
        trigger_monotonic_us=_u64(raw, 12),
        utc_unix_seconds=_i64(raw, 20),
        time_epoch_id=_u32(raw, 28),
        sample_rate_hz=_u32(raw, 32),
        trigger_sequence=_u32(raw, 36),
        pretrigger_samples=_u32(raw, 40),
        posttrigger_samples=_u32(raw, 44),
        subtrigger_count=_u16(raw, 48),
        flags=_u16(raw, 50),
        peak_magnitude_sq=_u32(raw, 52),
        threshold_magnitude_sq=_u32(raw, 56),
        payload_crc32=_u32(raw, 64),
        health_state=raw[68],
        power_state=raw[69],
        context_valid_flags=context_valid_flags,
        reset_raw_flags=_u32(raw, 72),
        temperature_centi_c=_i16(raw, 76),
        environment_age_seconds=_u16(raw, 78),
        humidity_milli_rh=_u32(raw, 80),
        free_log_bytes=_u32(raw, 84),
        sample_pool_min_free=_u16(raw, 88),
        last_fault_code=_u32(raw, 92),
        imu_transport_error_count=_u32(raw, 96),
        imu_dma_error_count=_u32(raw, 100),
        sample_pool_backpressure_count=_u32(raw, 104),
        storage_error_count=_u32(raw, 108),
        event_export_error_count=_u32(raw, 112),
        transition_sequence=_u32(raw, 124),
        lost_sample_count=_u32(raw, 128) if version == EV03_VERSION else 0,
        first_lost_sequence=_u32(raw, 132) if version == EV03_VERSION else 0,
        last_lost_sequence=_u32(raw, 136) if version == EV03_VERSION else 0,
        loss_episode_count=_u32(raw, 140) if version == EV03_VERSION else 0,
        first_loss_monotonic_us=_u64(raw, 144) if version == EV03_VERSION else 0,
        last_loss_monotonic_us=_u64(raw, 152) if version == EV03_VERSION else 0,
    )
    if metadata.sample_rate_hz == 0:
        raise EventFormatError("sample rate must be nonzero")
    has_loss_flag = bool(metadata.flags & EVENT_FLAG_DATA_LOSS)
    has_loss = metadata.lost_sample_count != 0
    if version == EV03_VERSION and has_loss_flag != has_loss:
        raise EventFormatError("DATA_LOSS flag does not match loss metadata")
    if version == EV03_VERSION and (
        (not has_loss and any((
            metadata.first_lost_sequence,
            metadata.last_lost_sequence,
            metadata.loss_episode_count,
            metadata.first_loss_monotonic_us,
            metadata.last_loss_monotonic_us,
        )))
        or (has_loss and (
            metadata.loss_episode_count == 0
            or metadata.loss_episode_count > metadata.lost_sample_count
        ))
    ):
        raise EventFormatError("loss metadata is internally inconsistent")
    return metadata, payload_length


def _freeze_event_arrays(event: DecodedEvent) -> None:
    for values in (
        event.accel_counts,
        event.gyro_counts,
        event.sensor_timestamps,
        event.sample_temperatures_c,
        event.fifo_headers,
        event.timestamps_us,
        event.accel_magnitude_counts,
    ):
        values.setflags(write=False)


def _u16(raw: bytes, offset: int) -> int:
    return struct.unpack_from("<H", raw, offset)[0]


def _i16(raw: bytes, offset: int) -> int:
    return struct.unpack_from("<h", raw, offset)[0]


def _u32(raw: bytes, offset: int) -> int:
    return struct.unpack_from("<I", raw, offset)[0]


def _u64(raw: bytes, offset: int) -> int:
    return struct.unpack_from("<Q", raw, offset)[0]


def _i64(raw: bytes, offset: int) -> int:
    return struct.unpack_from("<q", raw, offset)[0]
