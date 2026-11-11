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
#include "crsf_baud_cli.h"
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
static crsf_cli_t g_cli;
static uint32_t g_logged_frames = 0u;
static uint32_t g_last_rx_ms = 0u;
static uint32_t g_baud_in = CRSF_BAUD_IN;
static uint32_t g_baud_out = CRSF_BAUD_OUT;

/* Реальная скорость, на которой UART работает: делитель у RP2040 дробный,
 * поэтому «заказанная» и «фактическая» скорости могут чуть отличаться.
 * Печатаем обе — и предупреждаем, если расхождение заметное.
 *
 * getActualBaud() есть не во всех ревизиях ядра Arduino-Pico, поэтому ищем
 * метод через SFINAE: если его нет — сообщаем, что фактическая скорость
 * неизвестна, и ограничиваемся запрошенной. */
template <typename T>
static auto actual_baud_of(T &ser, int) -> decltype(ser.getActualBaud())
{
    return ser.getActualBaud();   /* метод есть в этой ревизии ядра */
}

template <typename T>
static int actual_baud_of(T &, long)
{
    return -1;   /* метода нет в этой ревизии ядра */
}

static void print_baud_pair(const char *tag, uint32_t wanted, int actual)
{
    if (actual < 0) {
        Serial.printf("%s: запрошено %lu бод (фактическая скорость в этой ревизии ядра недоступна)\n",
                      tag, (unsigned long)wanted);
        return;
    }
    if (actual == 0) {
        Serial.printf("%s: запрошено %lu бод, UART не запущен\n", tag, (unsigned long)wanted);
        return;
    }
    const uint32_t diff = (wanted > (uint32_t)actual) ? (wanted - (uint32_t)actual)
                                                      : ((uint32_t)actual - wanted);
    const uint32_t permille = (diff * 1000u) / (wanted ? wanted : 1u);
    Serial.printf("%s: запрошено %lu бод, фактически %d бод (расхождение %u.%u %%)\n",
                  tag, (unsigned long)wanted, actual, (unsigned)(permille / 10u),
                  (unsigned)(permille % 10u));
    if (permille > 30u) {
        Serial.printf("  ВНИМАНИЕ: %s: расхождение больше 3 %% — приёмник не удержит кадр\n", tag);
    }
}

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
    crsf_bridge_set_coalesce(&g_bridge, (uint8_t)(CRSF_BAUD_COALESCE ? 1 : 0));
    crsf_cli_init(&g_cli);

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
                  (unsigned long)g_baud_in, (unsigned)CRSF_BAUD_IN_UART,
                  (unsigned long)g_baud_out, (unsigned)CRSF_BAUD_OUT_UART,
                  (unsigned)CRSF_BAUD_QUEUE_SLOTS);
    print_baud_pair("вход", g_baud_in, actual_baud_of(s_in, 0));
    print_baud_pair("выход", g_baud_out, actual_baud_of(s_out, 0));
    Serial.println("ждём CRSF от приёмника...");
    Serial.println("подсказка: скорость на выходе должна ТОЧНО совпадать с настройкой полётника");
}

static void print_help(void)
{
    Serial.println("команды: help | status | stats | baud_in <n> | baud_out <n>");
    Serial.println("  baud_in/baud_out меняют скорость на лету (n = бод, например 420000)");
}

static void apply_baud_in(uint32_t baud)
{
    s_in.end();
    s_in.setPinout(CRSF_BAUD_IN_TX_PIN, CRSF_BAUD_IN_RX_PIN);
    s_in.begin(baud);
    g_baud_in = baud;
}

static void apply_baud_out(uint32_t baud)
{
    s_out.end();
    s_out.setPinout(CRSF_BAUD_OUT_TX_PIN, CRSF_BAUD_OUT_RX_PIN);
    s_out.begin(baud);
    g_baud_out = baud;
}

static void handle_command(const crsf_cli_result_t &res)
{
    switch (res.cmd) {
        case CRSF_CLI_HELP:
            print_help();
            break;
        case CRSF_CLI_STATUS:
            Serial.printf("статус: in=%lu бод (UART%u), out=%lu бод (UART%u), слотов=%u, прореживание=%u\n",
                          (unsigned long)g_baud_in, (unsigned)CRSF_BAUD_IN_UART,
                          (unsigned long)g_baud_out, (unsigned)CRSF_BAUD_OUT_UART,
                          (unsigned)CRSF_BAUD_QUEUE_SLOTS, (unsigned)CRSF_BAUD_COALESCE);
            print_baud_pair("вход", g_baud_in, actual_baud_of(s_in, 0));
            print_baud_pair("выход", g_baud_out, actual_baud_of(s_out, 0));
            break;
        case CRSF_CLI_STATS:
            print_stats("stats:");
            break;
        case CRSF_CLI_SET_BAUD_IN:
            apply_baud_in(res.value);
            Serial.printf("входная скорость: %lu бод\n", (unsigned long)g_baud_in);
            print_baud_pair("вход", g_baud_in, actual_baud_of(s_in, 0));
            break;
        case CRSF_CLI_SET_BAUD_OUT:
            apply_baud_out(res.value);
            Serial.printf("выходная скорость: %lu бод\n", (unsigned long)g_baud_out);
            print_baud_pair("выход", g_baud_out, actual_baud_of(s_out, 0));
            Serial.println("проверь, что полётник настроен ровно на эту скорость");
            break;
        case CRSF_CLI_UNKNOWN:
            Serial.println("не понял команду (help — список)");
            break;
        default:
            break;
    }
}

/* Команды с USB-консоли. */
static void service_console(void)
{
    while (Serial.available() > 0) {
        const char c = (char)Serial.read();
        crsf_cli_result_t res;
        if (crsf_cli_feed(&g_cli, c, &res)) {
            handle_command(res);
        }
    }
}

void loop()
{
    service_console();

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
