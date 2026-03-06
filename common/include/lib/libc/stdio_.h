#pragma once

#define sprintf(buf, fmt, ...) snprintf(buf, 64, fmt, ##__VA_ARGS__)