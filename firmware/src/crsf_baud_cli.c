#include "crsf_baud_cli.h"

#include <string.h>

static bool is_space(char c)
{
    return (c == ' ') || (c == '\t');
}

static bool is_digit(char c)
{
    return (c >= '0') && (c <= '9');
}

static bool equal_nocase(const char *a, const char *b, size_t len)
{
    for (size_t i = 0; i < len; ++i) {
        char ca = a[i];
        char cb = b[i];
        if (ca >= 'a' && ca <= 'z') { ca = (char)(ca - 'a' + 'A'); }
        if (cb >= 'a' && cb <= 'z') { cb = (char)(cb - 'a' + 'A'); }
        if (ca != cb) {
            return false;
        }
    }
    return true;
}

/* Разбор беззнакового числа; false — если цифр нет или значение вне диапазона. */
static bool parse_baud(const char *s, size_t len, uint32_t *out)
{
    while (len > 0 && is_space(*s)) { ++s; --len; }
    if (len == 0) {
        return false;
    }
    uint32_t value = 0;
    for (size_t i = 0; i < len; ++i) {
        if (!is_digit(s[i])) {
            return false;
        }
        value = (value * 10u) + (uint32_t)(s[i] - '0');
        if (value > CRSF_CLI_BAUD_MAX) {
            return false;
        }
    }
    if (value < CRSF_CLI_BAUD_MIN) {
        return false;
    }
    *out = value;
    return true;
}

void crsf_cli_init(crsf_cli_t *cli)
{
    if (cli == NULL) {
        return;
    }
    memset(cli, 0, sizeof(*cli));
}

bool crsf_cli_parse(const char *line, size_t len, crsf_cli_result_t *out)
{
    if (line == NULL || out == NULL) {
        return false;
    }
    out->cmd = CRSF_CLI_UNKNOWN;
    out->value = 0;

    /* Обрезаем пробелы по краям. */
    while (len > 0 && is_space(*line)) { ++line; --len; }
    while (len > 0 && is_space(line[len - 1u])) { --len; }
    if (len == 0) {
        return false;
    }

    if ((len == 4 && equal_nocase(line, "help", 4)) ||
        (len == 1 && line[0] == '?')) {
        out->cmd = CRSF_CLI_HELP;
        return true;
    }
    if (len == 6 && equal_nocase(line, "status", 6)) {
        out->cmd = CRSF_CLI_STATUS;
        return true;
    }
    if (len == 5 && equal_nocase(line, "stats", 5)) {
        out->cmd = CRSF_CLI_STATS;
        return true;
    }
    if (len > 7 && equal_nocase(line, "baud_in", 7)) {
        uint32_t value = 0;
        if (!parse_baud(&line[7], len - 7u, &value)) {
            return true;   /* cmd = UNKNOWN */
        }
        out->cmd = CRSF_CLI_SET_BAUD_IN;
        out->value = value;
        return true;
    }
    if (len > 8 && equal_nocase(line, "baud_out", 8)) {
        uint32_t value = 0;
        if (!parse_baud(&line[8], len - 8u, &value)) {
            return true;
        }
        out->cmd = CRSF_CLI_SET_BAUD_OUT;
        out->value = value;
        return true;
    }
    return true;
}

bool crsf_cli_feed(crsf_cli_t *cli, char c, crsf_cli_result_t *out)
{
    if (cli == NULL || out == NULL) {
        return false;
    }
    if (c == '\r') {
        c = '\n';
    }
    if (c != '\n') {
        if (cli->len < (CRSF_CLI_LINE_MAX - 1u)) {
            cli->line[cli->len++] = c;
        } else {
            cli->overflow = true;   /* строка слишком длинная — отбросим её */
        }
        return false;
    }
    /* Терминатор: разбираем накопленное. */
    if (cli->overflow) {
        cli->len = 0;
        cli->overflow = false;
        out->cmd = CRSF_CLI_UNKNOWN;
        out->value = 0;
        return true;
    }
    const bool parsed = crsf_cli_parse(cli->line, cli->len, out);
    cli->len = 0;
    return parsed;
}
