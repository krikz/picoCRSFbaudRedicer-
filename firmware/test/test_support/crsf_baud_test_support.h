/* Минимальный Unity-шим для нативных тестов.
 *
 * PlatformIO в env native_test линкуется с настоящим Unity, поэтому там этот
 * файл просто пробрасывает макросы. Но логику моста удобно проверять и обычным
 * gcc — без PlatformIO и без скачивания тулчейна. Для этого при сборке
 * определяется CRSF_BAUD_TEST_STANDALONE, и включается маленькая
 * самодостаточная реализация нужных макросов.
 *
 * Образец: GOODWORKRINKZ/biba, firmware/test/test_support/biba_test_support.h
 */

#ifndef CRSF_BAUD_TEST_SUPPORT_H
#define CRSF_BAUD_TEST_SUPPORT_H

#if defined(CRSF_BAUD_TEST_STANDALONE)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

extern int crsf_baud_test_failures;
extern int crsf_baud_test_cases;
extern const char *crsf_baud_test_current_name;

#define UNITY_BEGIN() do { crsf_baud_test_failures = 0; crsf_baud_test_cases = 0; } while (0)

#define UNITY_END() (crsf_baud_test_failures)

#define RUN_TEST(fn) do { \
    crsf_baud_test_current_name = #fn; \
    int prev_failures = crsf_baud_test_failures; \
    crsf_baud_test_cases += 1; \
    fn(); \
    if (crsf_baud_test_failures != prev_failures) { \
        fprintf(stderr, "FAIL %s\n", #fn); \
    } else { \
        fprintf(stdout, "ok   %s\n", #fn); \
    } \
} while (0)

#define CRSF_BAUD_FAIL(...) do { \
    crsf_baud_test_failures += 1; \
    fprintf(stderr, "  in %s: ", crsf_baud_test_current_name ? crsf_baud_test_current_name : "?"); \
    fprintf(stderr, __VA_ARGS__); \
    fprintf(stderr, "\n"); \
} while (0)

#define TEST_ASSERT_TRUE(cond) do { if (!(cond)) CRSF_BAUD_FAIL("ожидалось true: %s", #cond); } while (0)
#define TEST_ASSERT_FALSE(cond) do { if ((cond)) CRSF_BAUD_FAIL("ожидалось false: %s", #cond); } while (0)
#define TEST_ASSERT_EQUAL_INT(expected, actual) do { \
    long long _e = (long long)(expected); long long _a = (long long)(actual); \
    if (_e != _a) CRSF_BAUD_FAIL("ожидалось %lld, получено %lld (%s vs %s)", _e, _a, #expected, #actual); \
} while (0)
#define TEST_ASSERT_EQUAL_UINT(expected, actual) TEST_ASSERT_EQUAL_INT(expected, actual)
#define TEST_ASSERT_EQUAL_UINT8(expected, actual) TEST_ASSERT_EQUAL_INT(expected, actual)
#define TEST_ASSERT_EQUAL_UINT16(expected, actual) TEST_ASSERT_EQUAL_INT(expected, actual)
#define TEST_ASSERT_EQUAL_size_t(expected, actual) TEST_ASSERT_EQUAL_INT(expected, actual)
#define TEST_ASSERT_EQUAL_MEMORY(expected, actual, len) do { \
    if (memcmp((expected), (actual), (len)) != 0) \
        CRSF_BAUD_FAIL("несовпадение памяти на %zu байтах", (size_t)(len)); \
} while (0)
#define TEST_ASSERT_NOT_NULL(ptr) do { if ((ptr) == NULL) CRSF_BAUD_FAIL("ожидался не-NULL: %s", #ptr); } while (0)

#define CRSF_BAUD_TEST_STANDALONE_MAIN(runner) \
    int crsf_baud_test_failures = 0; \
    int crsf_baud_test_cases = 0; \
    const char *crsf_baud_test_current_name = NULL; \
    int main(void) { \
        UNITY_BEGIN(); \
        runner(); \
        int failed = UNITY_END(); \
        fprintf(stdout, "прогнано %d тестов, провалов: %d\n", crsf_baud_test_cases, failed); \
        return failed == 0 ? 0 : 1; \
    }

#ifdef __cplusplus
}
#endif

#else /* CRSF_BAUD_TEST_STANDALONE */

#include <unity.h>

#endif /* CRSF_BAUD_TEST_STANDALONE */

#endif /* CRSF_BAUD_TEST_SUPPORT_H */
