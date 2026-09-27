#ifndef TRANSPORT_RECORDER_BOOTLOADER_VERSION_H
#define TRANSPORT_RECORDER_BOOTLOADER_VERSION_H

#include "ota_package.h"

/* Major.minor.patch, one unsigned byte per component, compared numerically. */
#define BOOTLOADER_VERSION_CURRENT OTA_PACKAGE_VERSION_PACK(1U, 0U, 0U)
#define BOOTLOADER_VERSION_PACK(major, minor, patch) \
    OTA_PACKAGE_VERSION_PACK((major), (minor), (patch))

#endif
