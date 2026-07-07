#pragma once

#define ALIGN_UP(n, align) (((n) + (align) - 1) & ~((align) - 1))

#define BIT(n) (1UL << (n))
#define BIT2(n) (BIT(n) << 1 * 8)
#define BIT3(n) (BIT(n) << 2 * 8)
#define BIT4(n) (BIT(n) << 3 * 8)

#define MIN2(a, b) ((a) < (b) ? (a) : (b))
#define MAX2(a, b) ((a) > (b) ? (a) : (b))

#if defined(__GNUC__) || defined(__clang__)
#define POW2(n) ((n) <= 1 ? 2 : 1u << (32 - __builtin_clz((n) - 1)))
#define LOWEST_BIT(mask) __builtin_ctz(mask)
#define HIGHEST_BIT(mask) (31 - __builtin_clz(mask))
#else
#define POW2_B1(n) ((n) | ((n) >> 1))
#define POW2_B2(n) (POW2_B1(n) | (POW2_B1(n) >> 2))
#define POW2_B3(n) (POW2_B2(n) | (POW2_B2(n) >> 4))
#define POW2_B4(n) (POW2_B3(n) | (POW2_B3(n) >> 8))
#define POW2(n) ((n) <= 1 ? 2 : (POW2_B4((n) - 1) + 1)) // covers 16 bits for up to max 65,535
#define LOWEST_BIT(mask) \
    ({ \
        uint32_t _mask = (mask); \
        uint32_t _pos = 0; \
        while ((_mask & 1) == 0 && _pos < 32) { \
            _mask >>= 1; \
            _pos++; \
        } \
        _pos; \
    })
#define HIGHEST_BIT(mask) \
    ({ \
        uint32_t _mask = (mask); \
        uint32_t _pos = 31; \
        while ((_mask & (1UL << 31)) == 0 && _pos > 0) { \
            _mask <<= 1; \
            _pos--; \
        } \
        _pos; \
    })
#endif

#ifdef DEBUG
#define DEF_BREADCRUMB(breadcrumb, size) uint##size##_t breadcrumb = 0
#define ADD_BREADCRUMB(breadcrumb, step) breadcrumb |= BIT(step)
#define RST_BREADCRUMB(breadcrumb) breadcrumb = 0
#else
#define DEF_BREADCRUMB(breadcrumb, size) \
    do { \
    } while (0)
#define ADD_BREADCRUMB(breadcrumb, step) \
    do { \
    } while (0)
#define RST_BREADCRUMB(breadcrumb) \
    do { \
    } while (0)
#endif

#define STR2(x) #x // Helper macro for stringification
// Macro to convert a macro value to a string literal, e.g., STR(TEMP_TARGET) -> "125"
#define STR(x) STR2(x)
