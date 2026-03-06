#pragma once

typedef int (*test_func_t)(char *args);

typedef struct test_desc_s {
    const char *name;
    test_func_t func;
    bool        enabled;
} test_desc_t;
