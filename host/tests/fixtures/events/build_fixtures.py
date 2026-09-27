"""Generate deterministic EV02/EV03 fixture files used by host-only tests."""

from __future__ import annotations

from pathlib import Path
import struct
import zlib


SAMPLE = struct.Struct("<hhhhhhHbB")
SAMPLES = (
    (-7, 3, 1, 10, 11, 12, 100, 20, 0x68),
    (97, 4, 2, 13, 14, 15, 101, 21, 0x68),
    (-5, 5, 3, 16, 17, 18, 102, 22, 0x68),
    (6, 6, 4, 19, 20, 21, 103, 23, 0x68),
)


def build_ev02(
    *,
    version: int = 2,
    payload_crc_delta: int = 0,
    truncate_bytes: int = 0,
) -> bytes:
    payload = b"".join(SAMPLE.pack(*sample) for sample in SAMPLES)
    checksum = zlib.crc32(payload) & 0xFFFFFFFF
    header = bytearray(128)
    struct.pack_into("<4sHHI", header, 0, b"EV02", version, 128, 42)
    struct.pack_into(
        "<QqIIIIIHHIIII",
        header,
        12,
        1_000_000,
        1_700_000_000,
        1,
        1600,
        77,
        1,
        3,
        0,
        0,
        9_409,
        4_096,
        len(payload),
        checksum ^ payload_crc_delta,
    )
    return bytes(header) + payload[:-truncate_bytes or None]


def build_ev03() -> bytes:
    payload = b"".join(SAMPLE.pack(*sample) for sample in SAMPLES)
    checksum = zlib.crc32(payload) & 0xFFFFFFFF
    header = bytearray(160)
    struct.pack_into("<4sHHI", header, 0, b"EV03", 3, 160, 43)
    struct.pack_into(
        "<QqIIIIIHHIIII",
        header,
        12,
        2_000_000,
        1_700_000_001,
        2,
        1600,
        101,
        1,
        3,
        0,
        1 << 3,
        9_409,
        4_096,
        len(payload),
        checksum,
    )
    struct.pack_into("<IIIIQQ", header, 128, 6, 102, 109, 2, 2_001_250, 2_005_625)
    return bytes(header) + payload


def write_fixtures(directory: Path) -> None:
    directory.mkdir(parents=True, exist_ok=True)
    (directory / "valid-42.terp-event").write_bytes(build_ev02())
    (directory / "crc-invalid-42.terp-event").write_bytes(
        build_ev02(payload_crc_delta=1)
    )
    (directory / "truncated-42.terp-event").write_bytes(build_ev02(truncate_bytes=1))
    (directory / "unknown-version-42.terp-event").write_bytes(build_ev02(version=3))
    (directory / "valid-v3-loss-43.terp-event").write_bytes(build_ev03())


if __name__ == "__main__":
    write_fixtures(Path(__file__).parent)
