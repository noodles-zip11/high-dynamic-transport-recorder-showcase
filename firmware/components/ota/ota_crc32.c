#include <stddef.h>

#include "ota_crc32.h"

void ota_crc32_init(ota_crc32_t *context)
{
    if (context != NULL)
    {
        context->value = UINT32_C(0xFFFFFFFF);
    }
}

void ota_crc32_update(ota_crc32_t *context, const uint8_t *data, uint32_t length)
{
    uint32_t index;

    if (context == NULL || (data == NULL && length != 0U))
    {
        return;
    }

    for (index = 0U; index < length; index++)
    {
        uint32_t bit;

        context->value ^= data[index];
        for (bit = 0U; bit < 8U; bit++)
        {
            context->value = (context->value >> 1U)
                             ^ ((context->value & UINT32_C(1)) != 0U
                                ? UINT32_C(0xEDB88320) : UINT32_C(0));
        }
    }
}

uint32_t ota_crc32_final(const ota_crc32_t *context)
{
    return context == NULL ? 0U : (context->value ^ UINT32_C(0xFFFFFFFF));
}

uint32_t ota_crc32_compute(const uint8_t *data, uint32_t length)
{
    ota_crc32_t context;

    ota_crc32_init(&context);
    ota_crc32_update(&context, data, length);
    return ota_crc32_final(&context);
}
