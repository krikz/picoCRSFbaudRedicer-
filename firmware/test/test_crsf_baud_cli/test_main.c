/* Нативные тесты разбора команд USB-консоли. */

#include <string.h>

#include "crsf_baud_cli.h"
#include "crsf_baud_test_support.h"

static void feed_str(crsf_cli_t *cli, const char *s, crsf_cli_result_t *out, bool *got)
{
    for (const char *p = s; *p != '\0'; ++p) {
        if (crsf_cli_feed(cli, *p, out)) {
            *got = true;
        }
    }
}

static void test_help_command(void)
{
    crsf_cli_t cli;
    crsf_cli_result_t res;
    bool got = false;
    crsf_cli_init(&cli);
    feed_str(&cli, "help\n", &res, &got);
    TEST_ASSERT_TRUE(got);
    TEST_ASSERT_EQUAL_INT(CRSF_CLI_HELP, res.cmd);
}

static void test_status_and_stats(void)
{
    crsf_cli_t cli;
    crsf_cli_result_t res;
    bool got = false;
    crsf_cli_init(&cli);
    feed_str(&cli, "status\n", &res, &got);
    TEST_ASSERT_TRUE(got);
    TEST_ASSERT_EQUAL_INT(CRSF_CLI_STATUS, res.cmd);

    got = false;
    crsf_cli_init(&cli);
    feed_str(&cli, "stats\n", &res, &got);
    TEST_ASSERT_TRUE(got);
    TEST_ASSERT_EQUAL_INT(CRSF_CLI_STATS, res.cmd);
}

static void test_set_baud_in(void)
{
    crsf_cli_t cli;
    crsf_cli_result_t res;
    bool got = false;
    crsf_cli_init(&cli);
    feed_str(&cli, "baud_in 420000\n", &res, &got);
    TEST_ASSERT_TRUE(got);
    TEST_ASSERT_EQUAL_INT(CRSF_CLI_SET_BAUD_IN, res.cmd);
    TEST_ASSERT_EQUAL_UINT(420000u, res.value);
}

static void test_set_baud_out(void)
{
    crsf_cli_t cli;
    crsf_cli_result_t res;
    bool got = false;
    crsf_cli_init(&cli);
    feed_str(&cli, "baud_out 200000\n", &res, &got);
    TEST_ASSERT_TRUE(got);
    TEST_ASSERT_EQUAL_INT(CRSF_CLI_SET_BAUD_OUT, res.cmd);
    TEST_ASSERT_EQUAL_UINT(200000u, res.value);
}

static void test_case_insensitive_and_spaces(void)
{
    crsf_cli_t cli;
    crsf_cli_result_t res;
    bool got = false;
    crsf_cli_init(&cli);
    feed_str(&cli, "  BAUD_IN  416666  \n", &res, &got);
    TEST_ASSERT_TRUE(got);
    TEST_ASSERT_EQUAL_INT(CRSF_CLI_SET_BAUD_IN, res.cmd);
    TEST_ASSERT_EQUAL_UINT(416666u, res.value);
}

static void test_bad_baud_is_unknown(void)
{
    crsf_cli_t cli;
    crsf_cli_result_t res;
    bool got = false;
    crsf_cli_init(&cli);
    feed_str(&cli, "baud_in abc\n", &res, &got);
    TEST_ASSERT_TRUE(got);
    TEST_ASSERT_EQUAL_INT(CRSF_CLI_UNKNOWN, res.cmd);

    got = false;
    crsf_cli_init(&cli);
    feed_str(&cli, "baud_out 10\n", &res, &got);   /* ниже минимума */
    TEST_ASSERT_TRUE(got);
    TEST_ASSERT_EQUAL_INT(CRSF_CLI_UNKNOWN, res.cmd);
}

static void test_unknown_command(void)
{
    crsf_cli_t cli;
    crsf_cli_result_t res;
    bool got = false;
    crsf_cli_init(&cli);
    feed_str(&cli, "что-то\n", &res, &got);
    TEST_ASSERT_TRUE(got);
    TEST_ASSERT_EQUAL_INT(CRSF_CLI_UNKNOWN, res.cmd);
}

static void test_empty_line_is_not_a_command(void)
{
    crsf_cli_t cli;
    crsf_cli_result_t res;
    bool got = false;
    crsf_cli_init(&cli);
    feed_str(&cli, "\n", &res, &got);
    TEST_ASSERT_FALSE(got);
}

static void test_command_split_across_feeds(void)
{
    crsf_cli_t cli;
    crsf_cli_result_t res;
    bool got = false;
    crsf_cli_init(&cli);
    feed_str(&cli, "baud_", &res, &got);
    TEST_ASSERT_FALSE(got);
    feed_str(&cli, "in 4000", &res, &got);
    TEST_ASSERT_FALSE(got);
    feed_str(&cli, "00\n", &res, &got);
    TEST_ASSERT_TRUE(got);
    TEST_ASSERT_EQUAL_INT(CRSF_CLI_SET_BAUD_IN, res.cmd);
    TEST_ASSERT_EQUAL_UINT(400000u, res.value);
}

static void test_overlong_line_is_dropped(void)
{
    crsf_cli_t cli;
    crsf_cli_result_t res;
    bool got = false;
    crsf_cli_init(&cli);
    char long_line[128];
    memset(long_line, 'x', sizeof(long_line) - 1);
    long_line[sizeof(long_line) - 1] = '\0';
    feed_str(&cli, long_line, &res, &got);
    feed_str(&cli, "\n", &res, &got);
    TEST_ASSERT_TRUE(got);
    TEST_ASSERT_EQUAL_INT(CRSF_CLI_UNKNOWN, res.cmd);
}

static void run_all(void)
{
    RUN_TEST(test_help_command);
    RUN_TEST(test_status_and_stats);
    RUN_TEST(test_set_baud_in);
    RUN_TEST(test_set_baud_out);
    RUN_TEST(test_case_insensitive_and_spaces);
    RUN_TEST(test_bad_baud_is_unknown);
    RUN_TEST(test_unknown_command);
    RUN_TEST(test_empty_line_is_not_a_command);
    RUN_TEST(test_command_split_across_feeds);
    RUN_TEST(test_overlong_line_is_dropped);
}

#if defined(CRSF_BAUD_TEST_STANDALONE)
CRSF_BAUD_TEST_STANDALONE_MAIN(run_all)
#else
void setUp(void) {}
void tearDown(void) {}
int main(void) { UNITY_BEGIN(); run_all(); return UNITY_END(); }
#endif
