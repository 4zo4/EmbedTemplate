#pragma once

// The MAX Calculation Engine for up to 20 numbers
#define MAX3(a, ...) MAX2(a, MAX2(__VA_ARGS__))
#define MAX4(a, ...) MAX2(a, MAX3(__VA_ARGS__))
#define MAX5(a, ...) MAX2(a, MAX4(__VA_ARGS__))
#define MAX6(a, ...) MAX2(a, MAX5(__VA_ARGS__))
#define MAX7(a, ...) MAX2(a, MAX6(__VA_ARGS__))
#define MAX8(a, ...) MAX2(a, MAX7(__VA_ARGS__))
#define MAX9(a, ...) MAX2(a, MAX8(__VA_ARGS__))
#define MAX10(a, ...) MAX2(a, MAX9(__VA_ARGS__))
#define MAX11(a, ...) MAX2(a, MAX10(__VA_ARGS__))
#define MAX12(a, ...) MAX2(a, MAX11(__VA_ARGS__))
#define MAX13(a, ...) MAX2(a, MAX12(__VA_ARGS__))
#define MAX14(a, ...) MAX2(a, MAX13(__VA_ARGS__))
#define MAX15(a, ...) MAX2(a, MAX14(__VA_ARGS__))
#define MAX16(a, ...) MAX2(a, MAX15(__VA_ARGS__))
#define MAX17(a, ...) MAX2(a, MAX16(__VA_ARGS__))
#define MAX18(a, ...) MAX2(a, MAX17(__VA_ARGS__))
#define MAX19(a, ...) MAX2(a, MAX18(__VA_ARGS__))
#define MAX20(a, ...) MAX2(a, MAX19(__VA_ARGS__))

#define COUNT_ARGS(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, _17, _18, _19, _20, N, ...) N
#define GET_COUNT(...) COUNT_ARGS(__VA_ARGS__, 20, 19, 18, 17, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1)

#define MAX_NUM_DISPATCH(count, ...) MAX##count(__VA_ARGS__)
#define MAX_NUM_INNER(count, ...) MAX_NUM_DISPATCH(count, __VA_ARGS__)
#define MAX_NUM(...) MAX_NUM_INNER(GET_COUNT(__VA_ARGS__), __VA_ARGS__)
