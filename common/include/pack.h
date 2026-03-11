#pragma once

typedef union stream_u {
    uint32_t u32[2];
    uint16_t u16[4];
    uint8_t  u8[8];
    uint64_t all;
} stream_t;
