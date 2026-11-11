/* Нативные тесты неблокирующей очереди лога.
 *
 * Образец: GOODWORKRINKZ/biba, firmware/test/test_support/biba_test_support.h
 */

#include <stdio.h>
#include <string.h>

#include "crsf_baud_log.h"
#include "crsf_baud_test_support.h"

static crsf_log_t g_log;

/* Взять текущую строку и сравнить с ожидаемой. */
static void expect_current(const char *expected)
{
    size_t len = 0u;
    const char *cur = crsf_log_current(&g_log, &len);
    TEST_ASSERT_NOT_NULL(cur);
    TEST_ASSERT_EQUAL_STRING_LEN(expected, cur, strlen(expected));
    TEST_ASSERT_EQUAL_INT((int)strlen(expected), (int)len);
}

static void test_init_is_empty(void)
{
    crsf_log_init(&g_log);
    TEST_ASSERT_EQUAL_INT(0, crsf_log_pending(&g_log));
    TEST_ASSERT_EQUAL_INT(0, (int)crsf_log_overwritten(&g_log));
    TEST_ASSERT_NULL(crsf_log_current(&g_log, NULL));
}

static void test_push_then_take(void)
{
    crsf_log_init(&g_log);
    crsf_log_printf(&g_log, "in=%lu", 420000UL);

    TEST_ASSERT_EQUAL_INT(1, crsf_log_pending(&g_log));
    TEST_ASSERT_TRUE(crsf_log_take(&g_log));
    expect_current("in=420000");
    /* Взятая строка ушла из кольца. */
    TEST_ASSERT_EQUAL_INT(0, crsf_log_pending(&g_log));
}

static void test_take_on_empty_returns_false(void)
{
    crsf_log_init(&g_log);
    TEST_ASSERT_FALSE(crsf_log_take(&g_log));
    TEST_ASSERT_EQUAL_INT(0, crsf_log_pending(&g_log));
}

static void test_fifo_order_preserved(void)
{
    crsf_log_init(&g_log);
    for (int i = 0; i < 5; ++i) {
        crsf_log_printf(&g_log, "m%d", i);
    }
    TEST_ASSERT_EQUAL_INT(5, crsf_log_pending(&g_log));

    for (int i = 0; i < 5; ++i) {
        TEST_ASSERT_TRUE(crsf_log_take(&g_log));
        char expected[16];
        snprintf(expected, sizeof(expected), "m%d", i);
        expect_current(expected);
        crsf_log_current_done(&g_log);
    }
    TEST_ASSERT_EQUAL_INT(0, crsf_log_pending(&g_log));
}

static void test_take_blocks_until_current_done(void)
{
    crsf_log_init(&g_log);
    crsf_log_printf(&g_log, "first");
    crsf_log_printf(&g_log, "second");

    TEST_ASSERT_TRUE(crsf_log_take(&g_log));
    expect_current("first");

    /* Пока текущая строка не дослана, вторую брать нельзя — иначе она
     * потерялась бы при переполнении кольца. */
    TEST_ASSERT_FALSE(crsf_log_take(&g_log));
    TEST_ASSERT_EQUAL_INT(1, crsf_log_pending(&g_log));

    crsf_log_current_done(&g_log);
    TEST_ASSERT_TRUE(crsf_log_take(&g_log));
    expect_current("second");
}

static void test_overflow_drops_oldest_keeps_newest(void)
{
    crsf_log_init(&g_log);
    const int total = (int)CRSF_BAUD_LOG_SLOTS + 3;
    for (int i = 0; i < total; ++i) {
        crsf_log_printf(&g_log, "n%d", i);
    }

    TEST_ASSERT_EQUAL_INT((int)CRSF_BAUD_LOG_SLOTS, crsf_log_pending(&g_log));
    TEST_ASSERT_EQUAL_INT(3, (int)crsf_log_overwritten(&g_log));

    /* Самая старая вытеснена — первой в очереди теперь n3. */
    TEST_ASSERT_TRUE(crsf_log_take(&g_log));
    expect_current("n3");
}

/* РЕГРЕССИЯ: строка, которую уже взяли на отправку, не должна потеряться,
 * даже если в это время кольцо переполнилось. */
static void test_current_survives_overflow(void)
{
    crsf_log_init(&g_log);
    crsf_log_printf(&g_log, "in-flight");

    TEST_ASSERT_TRUE(crsf_log_take(&g_log));
    expect_current("in-flight");

    /* Пока «in-flight» не дослана, кольцо переполняем. */
    const int total = (int)CRSF_BAUD_LOG_SLOTS + 5;
    for (int i = 0; i < total; ++i) {
        crsf_log_printf(&g_log, "x%d", i);
    }
    TEST_ASSERT_EQUAL_INT((int)CRSF_BAUD_LOG_SLOTS, crsf_log_pending(&g_log));

    /* Текущая строка на месте и не изменилась. */
    expect_current("in-flight");

    /* И после завершения отправки в очереди остаются свежие строки. */
    crsf_log_current_done(&g_log);
    TEST_ASSERT_EQUAL_INT((int)CRSF_BAUD_LOG_SLOTS, crsf_log_pending(&g_log));
    TEST_ASSERT_TRUE(crsf_log_take(&g_log));
    expect_current("x5");
}

static void test_multiline_split_into_entries(void)
{
    crsf_log_init(&g_log);
    crsf_log_printf(&g_log, "first\nsecond\nthird");

    TEST_ASSERT_EQUAL_INT(3, crsf_log_pending(&g_log));

    TEST_ASSERT_TRUE(crsf_log_take(&g_log));
    expect_current("first");
    crsf_log_current_done(&g_log);

    TEST_ASSERT_TRUE(crsf_log_take(&g_log));
    expect_current("second");
    crsf_log_current_done(&g_log);

    TEST_ASSERT_TRUE(crsf_log_take(&g_log));
    expect_current("third");
}

static void test_empty_message_produces_nothing(void)
{
    crsf_log_init(&g_log);
    crsf_log_printf(&g_log, "");
    TEST_ASSERT_EQUAL_INT(0, crsf_log_pending(&g_log));

    crsf_log_printf(&g_log, "\n\n");
    TEST_ASSERT_EQUAL_INT(0, crsf_log_pending(&g_log));
}

static void test_long_line_is_truncated_not_overflowed(void)
{
    crsf_log_init(&g_log);

    char huge[CRSF_BAUD_LOG_LINE_MAX * 3];
    memset(huge, 'x', sizeof(huge) - 1u);
    huge[sizeof(huge) - 1u] = '\0';

    crsf_log_printf(&g_log, "%s", huge);

    TEST_ASSERT_TRUE(crsf_log_take(&g_log));
    size_t len = 0u;
    const char *cur = crsf_log_current(&g_log, &len);
    TEST_ASSERT_NOT_NULL(cur);
    TEST_ASSERT_EQUAL_INT((int)CRSF_BAUD_LOG_LINE_MAX - 1, (int)len);
}

static void test_current_done_on_nothing_is_safe(void)
{
    crsf_log_init(&g_log);
    crsf_log_current_done(&g_log);
    TEST_ASSERT_EQUAL_INT(0, crsf_log_pending(&g_log));
    TEST_ASSERT_NULL(crsf_log_current(&g_log, NULL));
}

static void run_all(void)
{
    RUN_TEST(test_init_is_empty);
    RUN_TEST(test_push_then_take);
    RUN_TEST(test_take_on_empty_returns_false);
    RUN_TEST(test_fifo_order_preserved);
    RUN_TEST(test_take_blocks_until_current_done);
    RUN_TEST(test_overflow_drops_oldest_keeps_newest);
    RUN_TEST(test_current_survives_overflow);
    RUN_TEST(test_multiline_split_into_entries);
    RUN_TEST(test_empty_message_produces_nothing);
    RUN_TEST(test_long_line_is_truncated_not_overflowed);
    RUN_TEST(test_current_done_on_nothing_is_safe);
}

#if defined(CRSF_BAUD_TEST_STANDALONE)
CRSF_BAUD_TEST_STANDALONE_MAIN(run_all)
#else
void setUp(void) {}
void tearDown(void) {}
int main(void) { UNITY_BEGIN(); run_all(); return UNITY_END(); }
#endif
