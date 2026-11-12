#include "crsf_baud_log.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

void crsf_log_init(crsf_log_t *log)
{
    if (log == NULL) {
        return;
    }
    memset(log, 0, sizeof(*log));
    log->head = 0u;
    log->count = 0u;
    log->overwritten = 0u;
    log->current_len = 0u;
    log->has_current = false;
}

/* Положить одну строку (без '\n') в кольцо. Если кольцо полно, вытесняется
 * самая старая строка — свежие данные важнее старых. */
static void log_push(crsf_log_t *log, const char *text, size_t len)
{
    crsf_log_slot_t *slot = &log->slots[log->head];

    if (len >= sizeof(slot->line)) {
        len = sizeof(slot->line) - 1u;
    }
    memcpy(slot->line, text, len);
    slot->line[len] = '\0';
    slot->len = len;

    log->head = (uint8_t)((log->head + 1u) % (uint8_t)CRSF_BAUD_LOG_SLOTS);

    if (log->count < (uint8_t)CRSF_BAUD_LOG_SLOTS) {
        log->count++;
    } else {
        /* Кольцо полно: слот, который мы только что записали, — это и есть
         * самая старая неотправленная строка. Она вытесняется. */
        log->overwritten++;
    }
}

void crsf_log_printf(crsf_log_t *log, const char *fmt, ...)
{
    if (log == NULL || fmt == NULL) {
        return;
    }

    char buf[CRSF_BAUD_LOG_LINE_MAX];
    va_list ap;
    va_start(ap, fmt);
    int written = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (written < 0) {
        return;
    }

    size_t len = (size_t)written;
    if (len >= sizeof(buf)) {
        len = sizeof(buf) - 1u;   /* обрезаем, а не выходим за пределы */
        buf[len] = '\0';
    }

    /* Сообщение может содержать несколько строк — каждая становится
     * отдельной записью кольца (так их удобнее отдавать host'у). */
    size_t start = 0u;
    for (size_t i = 0u; i <= len; ++i) {
        if (i == len || buf[i] == '\n') {
            if (i > start) {
                log_push(log, &buf[start], i - start);
            }
            start = i + 1u;
        }
    }
}

uint8_t crsf_log_pending(const crsf_log_t *log)
{
    return (log == NULL) ? 0u : log->count;
}

uint32_t crsf_log_overwritten(const crsf_log_t *log)
{
    return (log == NULL) ? 0u : log->overwritten;
}

bool crsf_log_take(crsf_log_t *log)
{
    if (log == NULL) {
        return false;
    }
    if (log->has_current) {
        return false;   /* текущая строка ещё не дослана */
    }
    if (log->count == 0u) {
        return false;
    }

    /* Самая старая неотправленная строка — слот перед head. */
    uint8_t tail = (uint8_t)((log->head + (uint8_t)CRSF_BAUD_LOG_SLOTS -
                               log->count) % (uint8_t)CRSF_BAUD_LOG_SLOTS);
    const crsf_log_slot_t *slot = &log->slots[tail];

    size_t len = slot->len;
    if (len >= sizeof(log->current)) {
        len = sizeof(log->current) - 1u;
    }
    memcpy(log->current, slot->line, len);
    log->current[len] = '\0';
    log->current_len = len;
    log->has_current = true;

    /* Строка ушла из кольца — переполнение её больше не тронет. */
    log->count--;
    return true;
}

const char *crsf_log_current(const crsf_log_t *log, size_t *len)
{
    if (log == NULL || !log->has_current) {
        if (len != NULL) {
            *len = 0u;
        }
        return NULL;
    }
    if (len != NULL) {
        *len = log->current_len;
    }
    return log->current;
}

void crsf_log_current_done(crsf_log_t *log)
{
    if (log == NULL) {
        return;
    }
    log->current_len = 0u;
    log->has_current = false;
}
