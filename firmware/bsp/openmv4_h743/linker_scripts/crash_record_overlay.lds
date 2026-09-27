SECTIONS
{
    .crash_record 0x3800FE00 (NOLOAD) : ALIGN(256)
    {
        __crash_record_start = .;
        __crash_record_slot_a = .;
        . = . + 0x100;
        __crash_record_slot_b = .;
        . = . + 0x100;
        __crash_record_end = .;
    }

    ASSERT(ADDR(.crash_record) == 0x3800FE00,
           "CrashRecord section base changed")
    ASSERT(ALIGN(SIZEOF(.crash_record), 256) == 0x200,
           "CrashRecord section must reserve exactly 512 bytes")
    ASSERT((__crash_record_slot_a & 0xFF) == 0,
           "CrashRecord slot A must be 256-byte aligned")
    ASSERT((__crash_record_slot_b & 0xFF) == 0,
           "CrashRecord slot B must be 256-byte aligned")
    ASSERT(__crash_record_end <= 0x38010000,
           "CrashRecord section exceeds retained D3 SRAM4")
}
