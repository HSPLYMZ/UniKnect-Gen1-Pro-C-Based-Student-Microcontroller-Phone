#ifndef TEST_COMMON_H
#define TEST_COMMON_H
#include <stddef.h>
#include <stdint.h>
__attribute__((noreturn)) void test_finish(int status);
#define CHECK(expr) do { if (!(expr)) test_finish(__LINE__); } while (0)
#endif
