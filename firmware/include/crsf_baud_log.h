#ifndef CRSF_BAUD_LOG_H
#define CRSF_BAUD_LOG_H

/* Неблокирующая очередь строк для USB-консоли. Модуль переносимый (C99),
 * поэтому тестируется нативными тестами.
 *
 * Зачем: печать в USB-CDC буферизуется всего 64 байта, а строка статистики
 * длиннее. Serial.printf в таком случае БЛОКИРУЕТСЯ, пока хост не прочитает
 * данные — до секунды. За это время мост не читает CRSF, переполняется FIFO
 * приёмника и байты теряются: молча пропадает управление.
 *
 * Поэтому текст сначала кладётся в кольцевой буфер, а наружу отдаётся
 * порциями по мере освобождения места в CDC — без блокировки.
 *
 * Жизненный цикл строки:
 *   crsf_log_printf()  → строка в кольцо
 *   crsf_log_take()    → строка забрана из кольца в «текущую» (из кольца
 *                       больше не вытесняется никогда)
 *   crsf_log_current() → указатель на текущую строку и её длину
 *   crsf_log_current_done() → текущая строка отправлена, можно брать следующую
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Сколько строк держим. Статистика печатается раз в N кадров, при зависшем
 * хосте строки копятся; при переполнении вытесняется самая старая, чтобы
 * свежие данные (текущие скорости, счётчики) всегда были видны. */
#ifndef CRSF_BAUD_LOG_SLOTS
#  define CRSF_BAUD_LOG_SLOTS      16u
#endif

/* Максимальная длина одной строки вместе с '\0'. */
#ifndef CRSF_BAUD_LOG_LINE_MAX
#  define CRSF_BAUD_LOG_LINE_MAX   128u
#endif

typedef struct {
    char   line[CRSF_BAUD_LOG_LINE_MAX];
    size_t len;
} crsf_log_slot_t;

typedef struct {
    crsf_log_slot_t slots[CRSF_BAUD_LOG_SLOTS];
    uint8_t  head;       /* куда пишем следующую строку */
    uint8_t  count;      /* сколько строк ждут отправки (в кольце) */
    uint32_t overwritten; /* сколько строк вытеснено из-за переполнения */
    /* Строка, которая сейчас отправляется. Живёт вне кольца — переполнение
     * её уже не тронет. */
    char    current[CRSF_BAUD_LOG_LINE_MAX];
    size_t  current_len;
    bool    has_current;
} crsf_log_t;

void crsf_log_init(crsf_log_t *log);

/* Добавить строку. Переводы строк в конце ставятся автоматически.
 * Если очередь полна — вытесняется самая старая строка (счётчик растёт). */
void crsf_log_printf(crsf_log_t *log, const char *fmt, ...);

/* Сколько строк ждут отправки в кольце. */
uint8_t crsf_log_pending(const crsf_log_t *log);

/* Сколько строк вытеснено (хост не успевал читать). */
uint32_t crsf_log_overwritten(const crsf_log_t *log);

/* Забрать самую старую строку из кольца в «текущую».
 * Возвращает false, если кольцо пусто (или текущая строка ещё не дослана —
 * тогда её надо дописать через crsf_log_current_done()). */
bool crsf_log_take(crsf_log_t *log);

/* Текущая строка на отправке: указатель и длина. */
const char *crsf_log_current(const crsf_log_t *log, size_t *len);

/* Текущая строка отправлена целиком — можно брать следующую. */
void crsf_log_current_done(crsf_log_t *log);

#ifdef __cplusplus
}
#endif

#endif /* CRSF_BAUD_LOG_H */
