#include "crsf_baud_bridge.h"

#include <string.h>

#include "crsf.h"

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

        if (bridge->asm_len == 0u) {
            /* Ищем начало кадра: sync-байт 0xC8. Всё остальное — мусор. */
            if (byte == CRSF_SYNC_BYTE) {
                bridge->asm[0] = byte;
                bridge->asm_len = 1u;
                bridge->asm_crc = 0u;
            }
            continue;
        }

        bridge->asm[bridge->asm_len++] = byte;

        if (bridge->asm_len == 2u) {
            /* Второй байт — длина: длина кадра минус 2 (sync и сам length). */
            const uint8_t length = byte;
            if (length < 2u || length > CRSF_BRIDGE_MAX_FRAME - 2u) {
                bridge->asm_len = 0u;
                bridge->stats.frames_bad++;
            }
            continue;
        }

        const size_t total = (size_t)bridge->asm[1] + 2u; /* sync + len + тело */
        const size_t index = (size_t)bridge->asm_len - 1u;

        if (index < total - 1u) {
            /* Всё до предпоследнего байта входит в CRC. */
            bridge->asm_crc = crsf_crc8_update(bridge->asm_crc, byte);
            continue;
        }

        /* Последний байт кадра — сам CRC. */
        if (bridge->asm_crc != byte) {
            bridge->asm_len = 0u;
            bridge->stats.frames_bad++;
            continue;
        }

        const uint8_t type = bridge->asm[2];
        const uint8_t frame_len = bridge->asm_len;
        bridge->asm_len = 0u;

        if (bridge->slot_count == 0u) {
            bridge->stats.frames_ok++;
            continue;
        }

        /* Мест нет — вытесняем самый старый кадр: свежая команда важнее. */
        if (bridge->count >= bridge->slot_count) {
            bridge->head = (uint8_t)((bridge->head + 1u) % bridge->slot_count);
            bridge->count--;
            bridge->stats.frames_dropped++;
        }

        crsf_bridge_slot_t *slot = &bridge->slots[bridge->head];
        memcpy(slot->data, bridge->asm, frame_len);
        slot->len = frame_len;
        bridge->head = (uint8_t)((bridge->head + 1u) % bridge->slot_count);
        bridge->count++;
        bridge->stats.frames_ok++;
        if (bridge->count > bridge->stats.queue_high_water) {
            bridge->stats.queue_high_water = bridge->count;
        }
        (void)type;
    }
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
