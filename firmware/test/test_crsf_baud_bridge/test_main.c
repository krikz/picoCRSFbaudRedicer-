/* Нативные тесты логики моста CRSF: сборка кадров, проверка CRC, очередь.
 *
 * Запуск без PlatformIO:
 *   gcc -std=gnu11 -DCRSF_BAUD_TEST_STANDALONE -Iinclude -Isrc -Itest/test_support \
 *       test/test_crsf_baud_bridge/test_main.c src/crsf_baud_bridge.c src/crsf.c \
 *       -o /tmp/crsf_baud_test && /tmp/crsf_baud_test
 */

#include <string.h>

#include "crsf_baud_bridge.h"
#include "crsf.h"
#include "crsf_baud_test_support.h"

/* Собрать корректный CRSF-кадр: [0xC8][len][type][payload][crc8].
 * len = тип + payload + crc; длина кадра = payload_len + 4. */
static uint8_t build_frame(uint8_t type, const uint8_t *payload, uint8_t payload_len, uint8_t *out)
{
    out[0] = CRSF_SYNC_BYTE;
    out[1] = (uint8_t)(payload_len + 2u);
    out[2] = type;
    if (payload_len > 0u) {
        memcpy(&out[3], payload, payload_len);
    }
    out[3u + payload_len] = crsf_crc8_dvb_s2(&out[2], (size_t)(payload_len + 1u));
    return (uint8_t)(payload_len + 4u);
}

static void test_crc8_known_vector(void)
{
    /* Контрольное значение CRC-8/DVB-S2 для "123456789" — 0xBC.
     * Параметры: poly 0xD5, init 0x00, refin/refout false, xorout 0x00
     * (каталог CRC, crc-catalogue / crc-calc). Полином 0xD5 — это НЕ check. */
    const uint8_t data[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    TEST_ASSERT_EQUAL_UINT8(0xBCu, crsf_crc8_dvb_s2(data, sizeof(data)));
}

static void test_frame_roundtrip_is_byte_exact(void)
{
    crsf_bridge_slot_t slots[8];
    crsf_bridge_t bridge;
    crsf_bridge_init(&bridge, slots, 8);

    uint8_t payload[16];
    for (unsigned i = 0; i < sizeof(payload); ++i) {
        payload[i] = (uint8_t)(0x10u + i);
    }
    uint8_t frame[32];
    const uint8_t len = build_frame(CRSF_FRAMETYPE_RC_CHANNELS, payload, 16u, frame);

    crsf_bridge_feed(&bridge, frame, len);
    TEST_ASSERT_EQUAL_UINT(1u, crsf_bridge_pending(&bridge));

    uint8_t out[CRSF_BRIDGE_MAX_FRAME];
    uint8_t out_len = 0;
    TEST_ASSERT_TRUE(crsf_bridge_pop(&bridge, out, &out_len));
    TEST_ASSERT_EQUAL_UINT8(len, out_len);
    TEST_ASSERT_EQUAL_MEMORY(frame, out, len);
}

static void test_bad_crc_is_rejected(void)
{
    crsf_bridge_slot_t slots[8];
    crsf_bridge_t bridge;
    crsf_bridge_init(&bridge, slots, 8);

    uint8_t frame[32];
    const uint8_t len = build_frame(CRSF_FRAMETYPE_RC_CHANNELS, NULL, 0u, frame);
    frame[len - 1u] ^= 0xFFu;   /* ломаем CRC */

    crsf_bridge_feed(&bridge, frame, len);
    TEST_ASSERT_EQUAL_UINT(0u, crsf_bridge_pending(&bridge));
    TEST_ASSERT_EQUAL_UINT(0u, crsf_bridge_stats(&bridge)->frames_ok);
    TEST_ASSERT_EQUAL_UINT(1u, crsf_bridge_stats(&bridge)->frames_bad);
}

static void test_invalid_length_is_rejected(void)
{
    crsf_bridge_slot_t slots[8];
    crsf_bridge_t bridge;
    crsf_bridge_init(&bridge, slots, 8);

    uint8_t frame[32];
    build_frame(CRSF_FRAMETYPE_BATTERY, NULL, 0u, frame);
    frame[1] = 0xFFu;   /* недопустимая длина */

    crsf_bridge_feed(&bridge, frame, 6u);
    TEST_ASSERT_EQUAL_UINT(0u, crsf_bridge_pending(&bridge));
    TEST_ASSERT_EQUAL_UINT(1u, crsf_bridge_stats(&bridge)->frames_bad);
}

static void test_noise_sync_recovers_after_timeout(void)
{
    crsf_bridge_slot_t slots[8];
    crsf_bridge_t bridge;
    crsf_bridge_init(&bridge, slots, 8);

    uint8_t frame[32];
    const uint8_t len = build_frame(CRSF_FRAMETYPE_LINK_STATS, NULL, 0u, frame);

    /* 0xC8 в шуме — ложный начало кадра: за ним ждём кадр длиной 0x22. */
    uint8_t noise[5] = {0x00u, 0x11u, 0xC8u, 0x22u, 0x33u};
    crsf_bridge_feed(&bridge, noise, sizeof(noise));
    crsf_bridge_feed(&bridge, frame, len);

    /* Пока таймаут не истёк, кандидат не разобран. */
    TEST_ASSERT_EQUAL_UINT(0u, crsf_bridge_pending(&bridge));

    /* По таймауту хвост от последнего sync-байта сохраняется, и настоящий
     * кадр (начавшийся внутри ложного) доходит до полётника. */
    crsf_bridge_expire(&bridge);
    TEST_ASSERT_EQUAL_UINT(1u, crsf_bridge_pending(&bridge));

    uint8_t out[CRSF_BRIDGE_MAX_FRAME];
    uint8_t out_len = 0;
    TEST_ASSERT_TRUE(crsf_bridge_pop(&bridge, out, &out_len));
    TEST_ASSERT_EQUAL_UINT8(CRSF_FRAMETYPE_LINK_STATS, out[2]);
    TEST_ASSERT_EQUAL_UINT8(len, out_len);
}

static void test_bad_crc_resyncs_to_next_frame(void)
{
    crsf_bridge_slot_t slots[8];
    crsf_bridge_t bridge;
    crsf_bridge_init(&bridge, slots, 8);

    uint8_t bad[32];
    uint8_t bad_len = build_frame(CRSF_FRAMETYPE_RC_CHANNELS, NULL, 0u, bad);
    bad[bad_len - 1u] ^= 0xFFu;

    uint8_t good[32];
    const uint8_t good_len = build_frame(CRSF_FRAMETYPE_LINK_STATS, NULL, 0u, good);

    /* Битый кадр и следующий хороший приходят подряд — ресинхронизация
     * по следующему sync-байту должна пропустить битый и пропустить хороший. */
    uint8_t stream[64];
    memcpy(stream, bad, bad_len);
    memcpy(&stream[bad_len], good, good_len);
    crsf_bridge_feed(&bridge, stream, (size_t)(bad_len + good_len));

    TEST_ASSERT_EQUAL_UINT(1u, crsf_bridge_pending(&bridge));
    TEST_ASSERT_EQUAL_UINT(1u, crsf_bridge_stats(&bridge)->frames_bad);
    TEST_ASSERT_EQUAL_UINT(1u, crsf_bridge_stats(&bridge)->frames_ok);

    uint8_t out[CRSF_BRIDGE_MAX_FRAME];
    uint8_t out_len = 0;
    TEST_ASSERT_TRUE(crsf_bridge_pop(&bridge, out, &out_len));
    TEST_ASSERT_EQUAL_UINT8(CRSF_FRAMETYPE_LINK_STATS, out[2]);
}

static void test_frame_split_across_feeds(void)
{
    crsf_bridge_slot_t slots[8];
    crsf_bridge_t bridge;
    crsf_bridge_init(&bridge, slots, 8);

    uint8_t frame[32];
    const uint8_t len = build_frame(CRSF_FRAMETYPE_RC_CHANNELS, NULL, 0u, frame);

    crsf_bridge_feed(&bridge, frame, 1u);
    crsf_bridge_feed(&bridge, &frame[1], (size_t)(len - 1u) / 2u);
    crsf_bridge_feed(&bridge, &frame[1 + (len - 1u) / 2u], (size_t)(len - 1u) - (len - 1u) / 2u);

    TEST_ASSERT_EQUAL_UINT(1u, crsf_bridge_pending(&bridge));
}

static void test_several_frames_in_one_feed(void)
{
    crsf_bridge_slot_t slots[8];
    crsf_bridge_t bridge;
    crsf_bridge_init(&bridge, slots, 8);

    uint8_t stream[64];
    uint8_t offset = 0u;
    uint8_t len = 0;
    len = build_frame(CRSF_FRAMETYPE_RC_CHANNELS, NULL, 0u, &stream[offset]);
    offset = (uint8_t)(offset + len);
    len = build_frame(CRSF_FRAMETYPE_LINK_STATS, NULL, 0u, &stream[offset]);
    offset = (uint8_t)(offset + len);
    len = build_frame(CRSF_FRAMETYPE_BATTERY, NULL, 0u, &stream[offset]);
    offset = (uint8_t)(offset + len);

    crsf_bridge_feed(&bridge, stream, offset);
    TEST_ASSERT_EQUAL_UINT(3u, crsf_bridge_pending(&bridge));
    TEST_ASSERT_EQUAL_UINT(3u, crsf_bridge_stats(&bridge)->frames_ok);
}

static void test_incomplete_tail_is_not_emitted(void)
{
    crsf_bridge_slot_t slots[8];
    crsf_bridge_t bridge;
    crsf_bridge_init(&bridge, slots, 8);

    uint8_t frame[32];
    const uint8_t len = build_frame(CRSF_FRAMETYPE_RC_CHANNELS, NULL, 0u, frame);

    crsf_bridge_feed(&bridge, frame, (size_t)(len - 1u));
    TEST_ASSERT_EQUAL_UINT(0u, crsf_bridge_pending(&bridge));
}

static void test_overflow_drops_oldest_frame(void)
{
    crsf_bridge_slot_t slots[2];
    crsf_bridge_t bridge;
    crsf_bridge_init(&bridge, slots, 2);

    uint8_t payload[2] = {0xA1u, 0xA2u};
    uint8_t frame[32];

    uint8_t len = build_frame(CRSF_FRAMETYPE_RC_CHANNELS, payload, 2u, frame);
    crsf_bridge_feed(&bridge, frame, len);
    len = build_frame(CRSF_FRAMETYPE_LINK_STATS, payload, 2u, frame);
    crsf_bridge_feed(&bridge, frame, len);
    len = build_frame(CRSF_FRAMETYPE_BATTERY, payload, 2u, frame);
    crsf_bridge_feed(&bridge, frame, len);

    /* Мест было 2 слота → вытеснен самый старый кадр (RC). */
    TEST_ASSERT_EQUAL_UINT(2u, crsf_bridge_pending(&bridge));
    TEST_ASSERT_EQUAL_UINT(1u, crsf_bridge_stats(&bridge)->frames_dropped);
    TEST_ASSERT_EQUAL_UINT(2u, crsf_bridge_stats(&bridge)->queue_high_water);

    uint8_t out[CRSF_BRIDGE_MAX_FRAME];
    uint8_t out_len = 0;
    TEST_ASSERT_TRUE(crsf_bridge_pop(&bridge, out, &out_len));
    TEST_ASSERT_EQUAL_UINT8(CRSF_FRAMETYPE_LINK_STATS, out[2]);

    TEST_ASSERT_TRUE(crsf_bridge_pop(&bridge, out, &out_len));
    TEST_ASSERT_EQUAL_UINT8(CRSF_FRAMETYPE_BATTERY, out[2]);
}

static void test_stats_counters(void)
{
    crsf_bridge_slot_t slots[4];
    crsf_bridge_t bridge;
    crsf_bridge_init(&bridge, slots, 4);

    uint8_t frame[32];
    const uint8_t len = build_frame(CRSF_FRAMETYPE_RC_CHANNELS, NULL, 0u, frame);
    crsf_bridge_feed(&bridge, frame, len);

    const crsf_bridge_stats_t *st = crsf_bridge_stats(&bridge);
    TEST_ASSERT_EQUAL_UINT(len, st->bytes_in);
    TEST_ASSERT_EQUAL_UINT(0u, st->bytes_out);
    TEST_ASSERT_EQUAL_UINT(1u, st->frames_ok);

    uint8_t out[CRSF_BRIDGE_MAX_FRAME];
    uint8_t out_len = 0;
    TEST_ASSERT_TRUE(crsf_bridge_pop(&bridge, out, &out_len));
    TEST_ASSERT_EQUAL_UINT(len, crsf_bridge_stats(&bridge)->bytes_out);

    crsf_bridge_reset_stats(&bridge);
    TEST_ASSERT_EQUAL_UINT(0u, crsf_bridge_stats(&bridge)->frames_ok);
}

static void test_parse_frame_reports_type(void)
{
    uint8_t frame[32];
    const uint8_t len = build_frame(CRSF_FRAMETYPE_LINK_STATS, NULL, 0u, frame);
    TEST_ASSERT_EQUAL_UINT8(CRSF_FRAMETYPE_LINK_STATS, crsf_parse_frame(frame, len));

    frame[len - 1u] ^= 0x01u;
    TEST_ASSERT_EQUAL_UINT8(0u, crsf_parse_frame(frame, len));
}


static void test_same_type_frame_replaces_older(void)
{
    crsf_bridge_slot_t slots[8];
    crsf_bridge_t bridge;
    crsf_bridge_init(&bridge, slots, 8);
    crsf_bridge_set_coalesce(&bridge, 1u);

    uint8_t payload[2] = {0x10u, 0x20u};
    uint8_t frame[32];
    uint8_t len = build_frame(CRSF_FRAMETYPE_RC_CHANNELS, payload, 2u, frame);
    crsf_bridge_feed(&bridge, frame, len);

    /* Второй кадр того же типа заменяет первый: в очереди остаётся свежий. */
    payload[0] = 0x33u;
    len = build_frame(CRSF_FRAMETYPE_RC_CHANNELS, payload, 2u, frame);
    crsf_bridge_feed(&bridge, frame, len);

    TEST_ASSERT_EQUAL_UINT(1u, crsf_bridge_pending(&bridge));
    TEST_ASSERT_EQUAL_UINT(1u, crsf_bridge_stats(&bridge)->frames_coalesced);

    uint8_t out[CRSF_BRIDGE_MAX_FRAME];
    uint8_t out_len = 0;
    TEST_ASSERT_TRUE(crsf_bridge_pop(&bridge, out, &out_len));
    TEST_ASSERT_EQUAL_UINT8(CRSF_FRAMETYPE_RC_CHANNELS, out[2]);
    TEST_ASSERT_EQUAL_UINT8(0x33u, out[3]);   /* свежие данные, не старые */
}

static void test_coalesce_keeps_other_types(void)
{
    crsf_bridge_slot_t slots[8];
    crsf_bridge_t bridge;
    crsf_bridge_init(&bridge, slots, 8);
    crsf_bridge_set_coalesce(&bridge, 1u);

    uint8_t frame[32];
    uint8_t len = build_frame(CRSF_FRAMETYPE_RC_CHANNELS, NULL, 0u, frame);
    crsf_bridge_feed(&bridge, frame, len);
    len = build_frame(CRSF_FRAMETYPE_LINK_STATS, NULL, 0u, frame);
    crsf_bridge_feed(&bridge, frame, len);

    /* Кадры разных типов не вытесняют друг друга. */
    TEST_ASSERT_EQUAL_UINT(2u, crsf_bridge_pending(&bridge));
    TEST_ASSERT_EQUAL_UINT(0u, crsf_bridge_stats(&bridge)->frames_coalesced);
}

static void test_without_coalesce_both_frames_stay(void)
{
    crsf_bridge_slot_t slots[8];
    crsf_bridge_t bridge;
    crsf_bridge_init(&bridge, slots, 8);
    crsf_bridge_set_coalesce(&bridge, 0u);

    uint8_t frame[32];
    uint8_t len = build_frame(CRSF_FRAMETYPE_RC_CHANNELS, NULL, 0u, frame);
    crsf_bridge_feed(&bridge, frame, len);
    len = build_frame(CRSF_FRAMETYPE_RC_CHANNELS, NULL, 0u, frame);
    crsf_bridge_feed(&bridge, frame, len);

    TEST_ASSERT_EQUAL_UINT(2u, crsf_bridge_pending(&bridge));
    TEST_ASSERT_EQUAL_UINT(0u, crsf_bridge_stats(&bridge)->frames_coalesced);
}

static void run_all(void)
{
    RUN_TEST(test_crc8_known_vector);
    RUN_TEST(test_frame_roundtrip_is_byte_exact);
    RUN_TEST(test_bad_crc_is_rejected);
    RUN_TEST(test_invalid_length_is_rejected);
    RUN_TEST(test_noise_sync_recovers_after_timeout);
    RUN_TEST(test_bad_crc_resyncs_to_next_frame);
    RUN_TEST(test_frame_split_across_feeds);
    RUN_TEST(test_several_frames_in_one_feed);
    RUN_TEST(test_incomplete_tail_is_not_emitted);
    RUN_TEST(test_overflow_drops_oldest_frame);
    RUN_TEST(test_stats_counters);
    RUN_TEST(test_parse_frame_reports_type);
    RUN_TEST(test_same_type_frame_replaces_older);
    RUN_TEST(test_coalesce_keeps_other_types);
    RUN_TEST(test_without_coalesce_both_frames_stay);
}

#if defined(CRSF_BAUD_TEST_STANDALONE)
CRSF_BAUD_TEST_STANDALONE_MAIN(run_all)
#else
void setUp(void) {}
void tearDown(void) {}
int main(void) { UNITY_BEGIN(); run_all(); return UNITY_END(); }
#endif
