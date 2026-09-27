#ifndef TRANSPORT_RECORDER_FAKE_NOR_H
#define TRANSPORT_RECORDER_FAKE_NOR_H

#include <stdint.h>

#include "nor_flash.h"

typedef struct
{
    nor_flash_t device;
    uint8_t *storage;
    uint32_t storage_bytes;
    uint32_t power_cut_after_bytes;
    uint32_t programmed_after_arm;
    uint32_t read_failure_after_successes;
    uint32_t successful_read_count;
    uint32_t read_call_count;
    uint64_t read_byte_count;
    uint32_t erase_call_count;
    rt_bool_t busy;
    rt_bool_t power_cut_armed;
    rt_bool_t power_cut_triggered;
    rt_bool_t erase_power_cut_armed;
    rt_bool_t erase_power_cut_triggered;
    rt_bool_t read_failure_armed;
} fake_nor_t;

rt_err_t fake_nor_init(fake_nor_t *fake,
                       uint8_t *storage,
                       uint32_t storage_bytes);
void fake_nor_set_power_cut(fake_nor_t *fake, uint32_t after_bytes);
void fake_nor_arm_power_cut(fake_nor_t *fake, uint32_t after_bytes);
void fake_nor_clear_power_cut(fake_nor_t *fake);
void fake_nor_arm_erase_power_cut(fake_nor_t *fake);
void fake_nor_clear_erase_power_cut(fake_nor_t *fake);
void fake_nor_set_busy(fake_nor_t *fake, rt_bool_t busy);
void fake_nor_set_read_failure_after(fake_nor_t *fake,
                                     uint32_t successful_read_count);
void fake_nor_reset_counters(fake_nor_t *fake);

#endif
