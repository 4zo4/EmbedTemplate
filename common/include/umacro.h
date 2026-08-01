#pragma once

#define MAX_SEQUENCE 8 // Maximum number of elements in the sequence supported

#define GET_SEQUENCE(N) SEQUENCE_##N
#define SEQUENCE_1 0
#define SEQUENCE_2 0, 1
#define SEQUENCE_3 0, 1, 2
#define SEQUENCE_4 0, 1, 2, 3
#define SEQUENCE_5 0, 1, 2, 3, 4
#define SEQUENCE_6 0, 1, 2, 3, 4, 5
#define SEQUENCE_7 0, 1, 2, 3, 4, 5, 6
#define SEQUENCE_8 0, 1, 2, 3, 4, 5, 6, 7

#define LIST_1(M, P, x) M(P, x)
#define LIST_2(M, P, x, ...) M(P, x) LIST_1(M, P, __VA_ARGS__)
#define LIST_3(M, P, x, ...) M(P, x) LIST_2(M, P, __VA_ARGS__)
#define LIST_4(M, P, x, ...) M(P, x) LIST_3(M, P, __VA_ARGS__)
#define LIST_5(M, P, x, ...) M(P, x) LIST_4(M, P, __VA_ARGS__)
#define LIST_6(M, P, x, ...) M(P, x) LIST_5(M, P, __VA_ARGS__)
#define LIST_7(M, P, x, ...) M(P, x) LIST_6(M, P, __VA_ARGS__)
#define LIST_8(M, P, x, ...) M(P, x) LIST_7(M, P, __VA_ARGS__)

#define COUNT_ARGS(_1, _2, _3, _4, _5, _6, _7, _8, N, ...) N
#define GET_COUNT(...) COUNT_ARGS(__VA_ARGS__, 8, 7, 6, 5, 4, 3, 2, 1)
#define LIST_CONCAT(prefix, count) prefix##count
#define LIST_DISPATCH(count, M, P, ...) LIST_CONCAT(LIST_, count)(M, P, __VA_ARGS__)
#define LIST_INNER(M, P, ...) LIST_DISPATCH(GET_COUNT(__VA_ARGS__), M, P, __VA_ARGS__)
#define CREATE_LIST(M, P, N) LIST_INNER(M, P, GET_SEQUENCE(N))
