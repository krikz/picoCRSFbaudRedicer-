#ifndef CRSF_BAUD_CLI_H
#define CRSF_BAUD_CLI_H

/* Разбор команд USB-консоли. Модуль переносимый (только C99), поэтому
 * разбирается и тестируется нативными тестами.
 *
 * Команды (регистр не важен, терминатор — \n или \r):
 *   help                 список команд
 *   status               текущие скорости и состояние
 *   stats                счётчики моста
 *   baud_in  <n>         новая скорость приёмника (например 420000)
 *   baud_out <n>         новая скорость полётника (например 230400)
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CRSF_CLI_LINE_MAX 48u

/* Допустимый диапазон скоростей UART, который имеет смысл принимать. */
#define CRSF_CLI_BAUD_MIN 9600UL
#define CRSF_CLI_BAUD_MAX 4000000UL

typedef enum {
    CRSF_CLI_NONE = 0,
    CRSF_CLI_HELP,
    CRSF_CLI_STATUS,
    CRSF_CLI_STATS,
    CRSF_CLI_SET_BAUD_IN,
    CRSF_CLI_SET_BAUD_OUT,
    CRSF_CLI_UNKNOWN
} crsf_cli_cmd_t;

typedef struct {
    crsf_cli_cmd_t cmd;
    uint32_t       value;   /* для SET_BAUD_* */
} crsf_cli_result_t;

typedef struct {
    char   line[CRSF_CLI_LINE_MAX];
    size_t len;
    bool   overflow;   /* строка длиннее лимита — будет отброшена целиком */
} crsf_cli_t;

void crsf_cli_init(crsf_cli_t *cli);

/* Скормить один байт с консоли. Возвращает true, когда накоплена целая
 * строка и она разобрана (в out — команда). */
bool crsf_cli_feed(crsf_cli_t *cli, char c, crsf_cli_result_t *out);

/* Разобрать готовую строку (без терминатора). */
bool crsf_cli_parse(const char *line, size_t len, crsf_cli_result_t *out);

#ifdef __cplusplus
}
#endif

#endif /* CRSF_BAUD_CLI_H */
