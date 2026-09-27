"""TERP v1 message identifiers shared by the host protocol modules."""

from enum import IntEnum

from .messages_generated import MessageType


RESPONSE_BIT = 0x8000
NOTIFICATION_BIT = 0x4000
TERP_CAPABILITY_RELIABILITY_EVIDENCE_V1 = 1 << 8


class ErrorCode(IntEnum):
    MALFORMED = 1
    UNSUPPORTED = 2
    NOT_FOUND = 3
    BUSY = 4
    INTERNAL = 5
    INCOMPATIBLE = 6
    HANDSHAKE_REQUIRED = 7


def response_type(message_type: int) -> int:
    return int(message_type) | RESPONSE_BIT


def is_known_message_type(message_type: int) -> bool:
    if int(message_type) == int(MessageType.ERROR):
        return True
    base_type = int(message_type) & ~(RESPONSE_BIT | NOTIFICATION_BIT)
    return base_type in {int(value) for value in MessageType}
