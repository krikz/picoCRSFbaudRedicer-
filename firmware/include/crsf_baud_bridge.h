#ifndef CRSF_BAUD_BRIDGE_H
#define CRSF_BAUD_BRIDGE_H

/* Мост «байты → CRSF-кадры»: принимает поток от приёмника, собирает и
 * проверяет кадры (sync + длина + CRC8) и отдаёт их целыми в очередь.
 *
 * Логика портируемая (только C99, без железа) — её покрывают нативные тесты.
 * Переполнение очереди решается вытеснением самого старого кадра: для RC
 * свежая команда важнее запоздалой.
 *
 * Спецификация CRSF: https://github.com/tbs-fpv/tbs-crsf-spec/blob/main/crsf.md
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Максимальная длина CRSF-кадра (64 байта вместе с sync и CRC). */
#define CRSF_BRIDGE_MAX_FRAME 64u

typedef struct {
    uint32_t frames_ok;        /* принято валидных кадров */
    uint32_t frames_bad;       /* отброшено: битый CRC / длина / мусор */
    uint32_t frames_dropped;   /* вытеснено из очереди (не хватило места) */
    uint32_t frames_coalesced; /* заменено более свежим кадром того же типа */
    uint32_t bytes_in;         /* всего байт со входа */
    uint32_t bytes_out;        /* всего байт на выход */
    uint32_t queue_high_water; /* максимум одновременно занятых слотов */
} crsf_bridge_stats_t;

typedef struct {
    uint8_t data[CRSF_BRIDGE_MAX_FRAME];
    uint8_t len;
} crsf_bridge_slot_t;

typedef struct {
    crsf_bridge_slot_t *slots;
    uint8_t             slot_count;
    uint8_t             head;     /* индекс самого старого кадра */
    uint8_t             tail;     /* индекс слота следующей записи */
    uint8_t             count;    /* занятых слотов */
    uint8_t             coalesce;   /* 1: кадр того же типа заменяет старый */
    uint8_t             accum_len;  /* сколько байт уже накоплено в accum */
    uint8_t             accum[CRSF_BRIDGE_MAX_FRAME];
    uint8_t             accum_crc;  /* CRC8 накопленного кадра */
    crsf_bridge_stats_t stats;
} crsf_bridge_t;

/* Инициализация: слоты очереди и счётчики. slots должен жить дольше bridge. */
void crsf_bridge_init(crsf_bridge_t *bridge, crsf_bridge_slot_t *slots, uint8_t slot_count);

/* Прореживание: 1 — новый кадр заменяет в очереди более старый кадр того же
 * типа (для RC это «оставить только свежую команду»). 0 — вытеснять только
 * самый старый кадр при переполнении. */
void crsf_bridge_set_coalesce(crsf_bridge_t *bridge, uint8_t enabled);

/* Скормить байты со входа. Кадры собираются, проверяются и кладутся в очередь. */
void crsf_bridge_feed(crsf_bridge_t *bridge, const uint8_t *data, size_t len);

/* Таймаут неполного кадра: вызывается хостом, если входящих байт не было
 * дольше CRSF_BAUD_FRAME_TIMEOUT_MS. Незавершённый кадр отбрасывается, но
 * хвост от последнего sync-байта сохраняется: настоящий кадр мог начаться
 * внутри ложного (шум в линии). */
void crsf_bridge_expire(crsf_bridge_t *bridge);

/* Забрать очередной готовый кадр. false — очередь пуста. */
bool crsf_bridge_pop(crsf_bridge_t *bridge, uint8_t *out, uint8_t *out_len);

/* Сколько кадров ждёт отправки. */
uint8_t crsf_bridge_pending(const crsf_bridge_t *bridge);

/* Счётчики. */
const crsf_bridge_stats_t *crsf_bridge_stats(const crsf_bridge_t *bridge);
void crsf_bridge_reset_stats(crsf_bridge_t *bridge);

#ifdef __cplusplus
}
#endif

#endif /* CRSF_BAUD_BRIDGE_H */
