/* picoCRSFbaudRedicer — преобразователь скорости линии CRSF (RP2040 / Pico).
 *
 *   приёмник Crossfire/ELRS --(CRSF, CRSF_BAUD_IN)--> [Pico] --> (CRSF, CRSF_BAUD_OUT)--> полётник
 *
 * Содержимое кадров не меняется: мост собирает кадры, проверяет CRC и отдаёт
 * их целыми. Меняется только скорость линии.
 *
 * Прошивка Arduino-Pico: Serial = USB CDC (логи), Serial1 = UART0 (GP4/GP5 по
 * умолчанию этого проекта), Serial2 = UART1 (GP8/GP9).
 *
 * Файл намеренно .cpp: Arduino-Pico объявляет Serial/Serial1/Serial2 как
 * C++-объекты, и в .c-файле заголовок Arduino.h теряет их объявления.
 */

#include <Arduino.h>

#include "target_config.h"      /* настройки таргета (подключается первым) */
#include "crsf_baud_config.h"   /* общий конфиг с умолчаниями */
#include "crsf_baud_bridge.h"
#include "crsf.h"

/* В ядре earlephilhower Serial1/Serial2 — это SerialUART (HW UART). Базовый
 * HardwareSerial умеет только begin(baud): пины задаются через
 * setPinout(tx, rx) — именно в таком порядке — и до begin(). */
#if CRSF_BAUD_IN_UART == 1
static SerialUART &s_in = Serial1;
#elif CRSF_BAUD_IN_UART == 2
static SerialUART &s_in = Serial2;
#else
#  error "CRSF_BAUD_IN_UART должен быть 1 (Serial1/UART0) или 2 (Serial2/UART1)"
#endif

#if CRSF_BAUD_OUT_UART == 1
static SerialUART &s_out = Serial1;
#elif CRSF_BAUD_OUT_UART == 2
static SerialUART &s_out = Serial2;
#else
#  error "CRSF_BAUD_OUT_UART должен быть 1 (Serial1/UART0) или 2 (Serial2/UART1)"
#endif

static crsf_bridge_slot_t g_slots[CRSF_BAUD_QUEUE_SLOTS];
static crsf_bridge_t g_bridge;
static uint32_t g_logged_frames = 0u;
static uint32_t g_last_rx_ms = 0u;

static void print_stats(const char *tag)
{
    const crsf_bridge_stats_t *st = crsf_bridge_stats(&g_bridge);
    if (st == NULL) {
        return;
    }
    Serial.printf("%s ok=%lu bad=%lu dropped=%lu in=%lu out=%lu q=%u/%lu\n",
                  tag,
                  (unsigned long)st->frames_ok,
                  (unsigned long)st->frames_bad,
                  (unsigned long)st->frames_dropped,
                  (unsigned long)st->bytes_in,
                  (unsigned long)st->bytes_out,
                  (unsigned)crsf_bridge_pending(&g_bridge),
                  (unsigned long)CRSF_BAUD_QUEUE_SLOTS);
}

void setup()
{
    Serial.begin(CRSF_BAUD_LOG_BAUD);

#if CRSF_BAUD_LED_ENABLE
    pinMode(CRSF_BAUD_LED_PIN, OUTPUT);
    digitalWrite(CRSF_BAUD_LED_PIN, LOW);
#endif

    crsf_bridge_init(&g_bridge, g_slots, (uint8_t)CRSF_BAUD_QUEUE_SLOTS);

    /* Пины: setPinout(tx, rx) — порядок именно такой. */
    if (!s_in.setPinout(CRSF_BAUD_IN_TX_PIN, CRSF_BAUD_IN_RX_PIN)) {
        Serial.println("ВНИМАНИЕ: не удалось назначить пины входного UART");
    }
    if (!s_out.setPinout(CRSF_BAUD_OUT_TX_PIN, CRSF_BAUD_OUT_RX_PIN)) {
        Serial.println("ВНИМАНИЕ: не удалось назначить пины выходного UART");
    }
    s_in.begin(CRSF_BAUD_IN);
    s_out.begin(CRSF_BAUD_OUT);

    Serial.printf("picoCRSFbaudRedicer: in=%lu бод (UART%u), out=%lu бод (UART%u), слотов=%u\n",
                  (unsigned long)CRSF_BAUD_IN, (unsigned)CRSF_BAUD_IN_UART,
                  (unsigned long)CRSF_BAUD_OUT, (unsigned)CRSF_BAUD_OUT_UART,
                  (unsigned)CRSF_BAUD_QUEUE_SLOTS);
    Serial.println("ждём CRSF от приёмника...");
}

void loop()
{
    /* 1. Забираем байты от приёмника и скормим их мосту. */
    uint8_t chunk[CRSF_BAUD_RX_CHUNK];
    int received = 0;
    while (received < (int)sizeof(chunk) && s_in.available() > 0) {
        chunk[received++] = (uint8_t)s_in.read();
    }
    if (received > 0) {
        crsf_bridge_feed(&g_bridge, chunk, (size_t)received);
        g_last_rx_ms = millis();
#if CRSF_BAUD_LED_ENABLE
        digitalWrite(CRSF_BAUD_LED_PIN, HIGH);   /* мигание при приёме */
#endif
    } else if ((uint32_t)(millis() - g_last_rx_ms) > CRSF_BAUD_FRAME_TIMEOUT_MS) {
        /* Данных нет дольше таймаута: незавершённый кадр отбрасываем,
         * чтобы ложный sync-байт в шуме не блокировал линию. */
        crsf_bridge_expire(&g_bridge);
        g_last_rx_ms = millis();
#if CRSF_BAUD_LED_ENABLE
        digitalWrite(CRSF_BAUD_LED_PIN, LOW);
#endif
    }

    /* 2. Отдаём накопленные кадры полётнику — целыми, побайтно идентичными. */
    uint8_t frame[CRSF_BRIDGE_MAX_FRAME];
    uint8_t frame_len = 0u;
    while (crsf_bridge_pop(&g_bridge, frame, &frame_len)) {
        s_out.write(frame, frame_len);
    }

#if CRSF_BAUD_ECHO_ENABLE
    /* 3. Необязательный обратный канал: полётник → приёмник. */
    int echo_received = 0;
    while (echo_received < (int)sizeof(chunk) && s_out.available() > 0) {
        chunk[echo_received++] = (uint8_t)s_out.read();
    }
    if (echo_received > 0) {
        s_in.write(chunk, (size_t)echo_received);
    }
#endif

#if CRSF_BAUD_LOG_EVERY_FRAMES > 0
    /* 4. Периодическая статистика. */
    const crsf_bridge_stats_t *st = crsf_bridge_stats(&g_bridge);
    if (st != NULL && st->frames_ok >= g_logged_frames + CRSF_BAUD_LOG_EVERY_FRAMES) {
        g_logged_frames = st->frames_ok;
        print_stats("crsf:");
    }
#endif
}
