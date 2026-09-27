import os


ARCH = "arm"
CPU = "cortex-m7"
CROSS_TOOL = "gcc"
PLATFORM = "gcc"
BSP_LIBRARY_TYPE = None

EXEC_PATH = os.environ.get(
    "RTT_EXEC_PATH",
    r"C:\ST\STM32CubeCLT_1.18.0\GNU-tools-for-STM32\bin",
)
PREFIX = "arm-none-eabi-"
CC = PREFIX + "gcc"
AS = PREFIX + "gcc"
AR = PREFIX + "ar"
CXX = PREFIX + "g++"
LINK = PREFIX + "gcc"
SIZE = PREFIX + "size"
OBJDUMP = PREFIX + "objdump"
OBJCPY = PREFIX + "objcopy"
TARGET_EXT = "elf"

DEVICE = " -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard"
BUILD_PROFILE = os.environ.get("TRANSPORT_BUILD_PROFILE", "debug").lower()
if BUILD_PROFILE not in {"debug", "release", "faultinjection"}:
    raise RuntimeError(
        "TRANSPORT_BUILD_PROFILE must be 'debug', 'release', or "
        "'faultinjection', "
        f"got {BUILD_PROFILE!r}"
    )

if BUILD_PROFILE == "release":
    OPTIMIZATION_FLAGS = " -O2 -g0 -DNDEBUG -DTRANSPORT_BUILD_RELEASE=1"
    ASSEMBLER_DEBUG_FLAGS = ""
else:
    OPTIMIZATION_FLAGS = " -O0 -gdwarf-2 -g"
    ASSEMBLER_DEBUG_FLAGS = " -gdwarf-2"

CFLAGS = DEVICE + " -ffunction-sections -fdata-sections" + OPTIMIZATION_FLAGS + " -Dgcc -std=c11"
CXXFLAGS = DEVICE + " -ffunction-sections -fdata-sections" + OPTIMIZATION_FLAGS
AFLAGS = " -c" + DEVICE + " -x assembler-with-cpp -Wa,-mimplicit-it=thumb" + ASSEMBLER_DEBUG_FLAGS
LFLAGS = (
    DEVICE
    + " -Wl,--gc-sections,-Map=build/transport_recorder.map,-cref,-u,Reset_Handler"
    + " -T bsp/openmv4_h743/linker_scripts/link.lds"
)
POST_ACTION = (
    OBJCPY
    + " -O binary $TARGET build/transport_recorder.bin\n"
    + SIZE
    + " $TARGET\n"
)
