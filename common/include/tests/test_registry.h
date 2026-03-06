#pragma once

typedef struct test_registry_s {
    const char  *name;
    test_desc_t *tests;
    int          count; // test count in tests
    bool         enabled;
} test_registry_t;

// Test Registry for SoC blocks
extern const int             dev_max_tests;
extern const test_registry_t dev_test_registry[];
// Test Registry for System blocks
extern const int             sys_max_tests;
extern const test_registry_t sys_test_registry[];