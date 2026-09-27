#include "bootloader_qspi_profile.h"

bool bootloader_qspi_profile_is_expected_jedec(uint32_t jedec_id)
{
    return jedec_id == BOOTLOADER_QSPI_EXPECTED_JEDEC;
}
