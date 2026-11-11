#include "crsf_baud_bridge.h"

#include <string.h>

#include "crsf.h"

/* Найти следующий sync-байт в накопленном буфере (начиная с позиции 1) и
 * оставить в буфере только хвост от него. false — sync-байтов больше нет. */
static bool resync(crsf_bridge_t *bridge)
{
    for (uint8_t i = 1u; i < bridge->accum_len; ++i) {
        if (bridge->accum[i] == CRSF_SYNC_BYTE) {
            const uint8_t n = (uint8_t)(bridge->accum_len - i);
            memmove(bridge->accum, &bridge->accum[i], n);
            bridge->accum_len = n;
            return true;
        }
    }
    bridge->accum_len = 0u;
    return false;
}

static void push_frame(crsf_bridge_t *bridge, uint8_t frame_len)
{
    if (bridge->slot_count == 0u) {
        bridge->stats.frames_ok++;
        return;
    }
    /* Мест нет — вытесняем самый старый кадр: свежая команда важнее запоздалой. */
    if (bridge->count >= bridge->slot_count) {
        bridge->head = (uint8_t)((bridge->head + 1u) % bridge->slot_count);
        bridge->count--;
        bridge->stats.frames_dropped++;
    }
    crsf_bridge_slot_t *slot = &bridge->slots[bridge->tail];
    memcpy(slot->data, bridge->accum, frame_len);
    slot->len = frame_len;
    bridge->tail = (uint8_t)((bridge->tail + 1u) % bridge->slot_count);
    bridge->count++;
    bridge->stats.frames_ok++;
    if (bridge->count > bridge->stats.queue_high_water) {
        bridge->stats.queue_high_water = bridge->count;
    }
}

/* Разобрать накопленные байты: дождаться полного кадра, проверить CRC.
 * При неудаче — ресинхронизация по следующему sync-байту. */
static void process_accum(crsf_bridge_t *bridge)
{
    for (;;) {
        if (bridge->accum_len < 2u) {
            return;
        }
        const uint8_t length = bridge->accum[1];
        if (length < 2u || length > (CRSF_BRIDGE_MAX_FRAME - 2u)) {
            /* Недопустимая длина — по спецификации такой кадр отбрасывается. */
            bridge->stats.frames_bad++;
            if (!resync(bridge)) {
                return;
            }
            continue;
        }
        const size_t total = (size_t)length + 2u;
        if ((size_t)bridge->accum_len < total) {
            return;   /* кадр ещё не весь */
        }
        if (crsf_crc8_dvb_s2(&bridge->accum[2], (size_t)length - 1u) != bridge->accum[total - 1u]) {
            bridge->stats.frames_bad++;
            if (!resync(bridge)) {
                return;
            }
            continue;
        }
        push_frame(bridge, (uint8_t)total);
        bridge->accum_len = 0u;
        return;
    }
}

void crsf_bridge_init(crsf_bridge_t *bridge, crsf_bridge_slot_t *slots, uint8_t slot_count)
{
    if (bridge == NULL) {
        return;
    }
    memset(bridge, 0, sizeof(*bridge));
    bridge->slots = slots;
    bridge->slot_count = slot_count;
}

void crsf_bridge_feed(crsf_bridge_t *bridge, const uint8_t *data, size_t len)
{
    if (bridge == NULL || data == NULL) {
        return;
    }
    for (size_t i = 0; i < len; ++i) {
        const uint8_t byte = data[i];
        bridge->stats.bytes_in++;

        if (bridge->accum_len == 0u) {
            /* Ждём sync-байт: всё остальное — мусор. */
            if (byte != CRSF_SYNC_BYTE) {
                continue;
            }
        } else if (bridge->accum_len >= CRSF_BRIDGE_MAX_FRAME) {
            /* Кадр длиннее максимума — отбрасываем кандидата. */
            bridge->accum_len = 0u;
            bridge->stats.frames_bad++;
            continue;
        }
        bridge->accum[bridge->accum_len++] = byte;
        process_accum(bridge);
    }
}

void crsf_bridge_expire(crsf_bridge_t *bridge)
{
    if (bridge == NULL || bridge->accum_len == 0u) {
        return;
    }
    /* Кандидат не завершился за таймаут. Сохраняем хвост от последнего
     * sync-байта: настоящий кадр мог начаться внутри ложного. */
    uint8_t last = 0u;
    bool found = false;
    for (uint8_t i = 0u; i < bridge->accum_len; ++i) {
        if (bridge->accum[i] == CRSF_SYNC_BYTE) {
            last = i;
            found = true;
        }
    }
    if (!found) {
        bridge->accum_len = 0u;
        return;
    }
    const uint8_t n = (uint8_t)(bridge->accum_len - last);
    memmove(bridge->accum, &bridge->accum[last], n);
    bridge->accum_len = n;
    process_accum(bridge);
}

bool crsf_bridge_pop(crsf_bridge_t *bridge, uint8_t *out, uint8_t *out_len)
{
    if (bridge == NULL || out == NULL || out_len == NULL || bridge->count == 0u) {
        return false;
    }
    const crsf_bridge_slot_t *slot = &bridge->slots[bridge->head];
    const uint8_t len = slot->len;
    memcpy(out, slot->data, len);
    *out_len = len;
    bridge->head = (uint8_t)((bridge->head + 1u) % bridge->slot_count);
    bridge->count--;
    bridge->stats.bytes_out += len;
    return true;
}

uint8_t crsf_bridge_pending(const crsf_bridge_t *bridge)
{
    return (bridge == NULL) ? 0u : bridge->count;
}

const crsf_bridge_stats_t *crsf_bridge_stats(const crsf_bridge_t *bridge)
{
    return (bridge == NULL) ? NULL : &bridge->stats;
}

void crsf_bridge_reset_stats(crsf_bridge_t *bridge)
{
    if (bridge == NULL) {
        return;
    }
    memset(&bridge->stats, 0, sizeof(bridge->stats));
}
