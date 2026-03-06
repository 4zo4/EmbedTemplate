#pragma once

#define malloc(size) (0)
#define free(ptr) \
    do { \
        (void)(ptr); \
    } while (0)
