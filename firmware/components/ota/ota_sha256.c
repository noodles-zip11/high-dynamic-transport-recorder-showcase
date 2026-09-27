#include <string.h>

#include "ota_sha256.h"

static const uint32_t ota_sha256_constants[64] = {
    UINT32_C(0x428A2F98), UINT32_C(0x71374491), UINT32_C(0xB5C0FBCF), UINT32_C(0xE9B5DBA5),
    UINT32_C(0x3956C25B), UINT32_C(0x59F111F1), UINT32_C(0x923F82A4), UINT32_C(0xAB1C5ED5),
    UINT32_C(0xD807AA98), UINT32_C(0x12835B01), UINT32_C(0x243185BE), UINT32_C(0x550C7DC3),
    UINT32_C(0x72BE5D74), UINT32_C(0x80DEB1FE), UINT32_C(0x9BDC06A7), UINT32_C(0xC19BF174),
    UINT32_C(0xE49B69C1), UINT32_C(0xEFBE4786), UINT32_C(0x0FC19DC6), UINT32_C(0x240CA1CC),
    UINT32_C(0x2DE92C6F), UINT32_C(0x4A7484AA), UINT32_C(0x5CB0A9DC), UINT32_C(0x76F988DA),
    UINT32_C(0x983E5152), UINT32_C(0xA831C66D), UINT32_C(0xB00327C8), UINT32_C(0xBF597FC7),
    UINT32_C(0xC6E00BF3), UINT32_C(0xD5A79147), UINT32_C(0x06CA6351), UINT32_C(0x14292967),
    UINT32_C(0x27B70A85), UINT32_C(0x2E1B2138), UINT32_C(0x4D2C6DFC), UINT32_C(0x53380D13),
    UINT32_C(0x650A7354), UINT32_C(0x766A0ABB), UINT32_C(0x81C2C92E), UINT32_C(0x92722C85),
    UINT32_C(0xA2BFE8A1), UINT32_C(0xA81A664B), UINT32_C(0xC24B8B70), UINT32_C(0xC76C51A3),
    UINT32_C(0xD192E819), UINT32_C(0xD6990624), UINT32_C(0xF40E3585), UINT32_C(0x106AA070),
    UINT32_C(0x19A4C116), UINT32_C(0x1E376C08), UINT32_C(0x2748774C), UINT32_C(0x34B0BCB5),
    UINT32_C(0x391C0CB3), UINT32_C(0x4ED8AA4A), UINT32_C(0x5B9CCA4F), UINT32_C(0x682E6FF3),
    UINT32_C(0x748F82EE), UINT32_C(0x78A5636F), UINT32_C(0x84C87814), UINT32_C(0x8CC70208),
    UINT32_C(0x90BEFFFA), UINT32_C(0xA4506CEB), UINT32_C(0xBEF9A3F7), UINT32_C(0xC67178F2),
};

static uint32_t ota_sha256_rotate_right(uint32_t value, uint32_t count)
{
    return (value >> count) | (value << (32U - count));
}

static uint32_t ota_sha256_read_u32_be(const uint8_t *data)
{
    return ((uint32_t)data[0] << 24U)
           | ((uint32_t)data[1] << 16U)
           | ((uint32_t)data[2] << 8U)
           | (uint32_t)data[3];
}

static void ota_sha256_write_u32_be(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)(value >> 24U);
    data[1] = (uint8_t)(value >> 16U);
    data[2] = (uint8_t)(value >> 8U);
    data[3] = (uint8_t)value;
}

static void ota_sha256_transform(ota_sha256_t *context, const uint8_t block[OTA_SHA256_BLOCK_BYTES])
{
    uint32_t schedule[64];
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t d;
    uint32_t e;
    uint32_t f;
    uint32_t g;
    uint32_t h;
    uint32_t index;

    for (index = 0U; index < 16U; index++)
    {
        schedule[index] = ota_sha256_read_u32_be(&block[index * 4U]);
    }
    for (index = 16U; index < 64U; index++)
    {
        const uint32_t s0 = ota_sha256_rotate_right(schedule[index - 15U], 7U)
                            ^ ota_sha256_rotate_right(schedule[index - 15U], 18U)
                            ^ (schedule[index - 15U] >> 3U);
        const uint32_t s1 = ota_sha256_rotate_right(schedule[index - 2U], 17U)
                            ^ ota_sha256_rotate_right(schedule[index - 2U], 19U)
                            ^ (schedule[index - 2U] >> 10U);

        schedule[index] = schedule[index - 16U] + s0 + schedule[index - 7U] + s1;
    }

    a = context->state[0];
    b = context->state[1];
    c = context->state[2];
    d = context->state[3];
    e = context->state[4];
    f = context->state[5];
    g = context->state[6];
    h = context->state[7];
    for (index = 0U; index < 64U; index++)
    {
        const uint32_t sum1 = ota_sha256_rotate_right(e, 6U)
                              ^ ota_sha256_rotate_right(e, 11U)
                              ^ ota_sha256_rotate_right(e, 25U);
        const uint32_t choice = (e & f) ^ ((~e) & g);
        const uint32_t temporary1 = h + sum1 + choice + ota_sha256_constants[index]
                                    + schedule[index];
        const uint32_t sum0 = ota_sha256_rotate_right(a, 2U)
                              ^ ota_sha256_rotate_right(a, 13U)
                              ^ ota_sha256_rotate_right(a, 22U);
        const uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        const uint32_t temporary2 = sum0 + majority;

        h = g;
        g = f;
        f = e;
        e = d + temporary1;
        d = c;
        c = b;
        b = a;
        a = temporary1 + temporary2;
    }

    context->state[0] += a;
    context->state[1] += b;
    context->state[2] += c;
    context->state[3] += d;
    context->state[4] += e;
    context->state[5] += f;
    context->state[6] += g;
    context->state[7] += h;
}

void ota_sha256_init(ota_sha256_t *context)
{
    if (context == NULL)
    {
        return;
    }

    context->state[0] = UINT32_C(0x6A09E667);
    context->state[1] = UINT32_C(0xBB67AE85);
    context->state[2] = UINT32_C(0x3C6EF372);
    context->state[3] = UINT32_C(0xA54FF53A);
    context->state[4] = UINT32_C(0x510E527F);
    context->state[5] = UINT32_C(0x9B05688C);
    context->state[6] = UINT32_C(0x1F83D9AB);
    context->state[7] = UINT32_C(0x5BE0CD19);
    context->total_length_bytes = 0U;
    context->block_length = 0U;
}

void ota_sha256_update(ota_sha256_t *context, const uint8_t *data, uint32_t length)
{
    uint32_t index;

    if (context == NULL || (data == NULL && length != 0U))
    {
        return;
    }

    for (index = 0U; index < length; index++)
    {
        context->block[context->block_length++] = data[index];
        if (context->block_length == OTA_SHA256_BLOCK_BYTES)
        {
            ota_sha256_transform(context, context->block);
            context->block_length = 0U;
        }
    }
    context->total_length_bytes += length;
}

void ota_sha256_final(ota_sha256_t *context, uint8_t digest[OTA_SHA256_DIGEST_BYTES])
{
    uint64_t bit_length;
    uint32_t index;

    if (context == NULL || digest == NULL)
    {
        return;
    }

    bit_length = context->total_length_bytes << 3U;
    context->block[context->block_length++] = UINT8_C(0x80);
    if (context->block_length > 56U)
    {
        memset(&context->block[context->block_length], 0,
               OTA_SHA256_BLOCK_BYTES - context->block_length);
        ota_sha256_transform(context, context->block);
        context->block_length = 0U;
    }
    memset(&context->block[context->block_length], 0, 56U - context->block_length);
    for (index = 0U; index < 8U; index++)
    {
        context->block[63U - index] = (uint8_t)(bit_length >> (index * 8U));
    }
    ota_sha256_transform(context, context->block);
    for (index = 0U; index < 8U; index++)
    {
        ota_sha256_write_u32_be(&digest[index * 4U], context->state[index]);
    }
}
