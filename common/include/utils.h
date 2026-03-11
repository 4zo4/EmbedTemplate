#pragma once

#define ALIGN_UP(n, align) (((n) + (align) - 1) & ~((align) - 1))

#define BIT(n) (1UL << (n))
#define BIT2(n) (BIT(n) << 8)

#define MIN2(a, b) ((a) < (b) ? (a) : (b))
#define MAX2(a, b) ((a) > (b) ? (a) : (b))

#if defined(__GNUC__) || defined(__clang__)
#define POW2(n) ((n) <= 1 ? 2 : 1u << (32 - __builtin_clz((n) - 1)))
#else
#define POW2_B1(n) ((n) | ((n) >> 1))
#define POW2_B2(n) (POW2_B1(n) | (POW2_B1(n) >> 2))
#define POW2_B3(n) (POW2_B2(n) | (POW2_B2(n) >> 4))
#define POW2_B4(n) (POW2_B3(n) | (POW2_B3(n) >> 8))
#define POW2(n) ((n) <= 1 ? 2 : (POW2_B4((n) - 1) + 1)) // covers 16 bits for up to max 65,535
#endif

#define STR2(x) #x // Helper macro for stringification
// Macro to convert a macro value to a string literal, e.g., STR(TEMP_TARGET) -> "125"
#define STR(x) STR2(x)
