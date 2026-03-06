
#pragma once

#if defined(__GNUC__) || defined(__clang__)
__attribute__((noreturn))
#endif
void __assert_func(const char *file, int line, const char *func, const char *expr);

#ifdef NDEBUG
#define assert(e) ((void)0)
#else
#define assert(e) \
    do { \
        if (!(e)) \
            __assert_func(__FILE__, __LINE__, __func__, #e); \
    } while (0)
#endif