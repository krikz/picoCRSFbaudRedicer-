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
#include "crsf_baud_log.h"
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
/* Отдельный мост для обратного канала (полётник → приёмник): свой буфер,
 * чтобы кадры туда-обратно не смешивались. */
static crsf_bridge_slot_t g_echo_slots[CRSF_BAUD_QUEUE_SLOTS];
static crsf_bridge_t g_echo_bridge;
static crsf_cli_t g_cli;
static crsf_log_t g_log;
static uint32_t g_logged_frames = 0u;
static uint32_t g_last_rx_ms = 0u;
static uint32_t g_uart_overruns = 0u;   /* переполнения FIFO приёма UART */
static uint32_t g_bytes_discarded = 0u; /* байты обратного канала, сброшенные */
/* Загрузка линии: сколько байт/с реально идёт по каждой стороне и какой
 * процент от физической скорости UART это занимает. Нужно, чтобы проверить
 * на живом железе, хватает ли выходной скорости: в односторонней схеме
 * выход несёт ВСЁ, что пришло от приёмника. */
static uint32_t g_util_in_bps = 0u;
static uint32_t g_util_out_bps = 0u;
static uint32_t g_util_window_ms = 0u;
static uint32_t g_util_last_in = 0u;
static uint32_t g_util_last_out = 0u;
/* Строка, которая сейчас отправляется в USB-консоль (может не влезть целиком). */
static char   g_out_line[CRSF_BAUD_LOG_LINE_MAX];
static size_t g_out_len = 0u;
static size_t g_out_off = 0u;
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
        crsf_log_printf(&g_log, "%s: запрошено %lu бод (фактическая скорость в этой ревизии ядра недоступна)",
                        tag, (unsigned long)wanted);
        return;
    }
    if (actual == 0) {
        crsf_log_printf(&g_log, "%s: запрошено %lu бод, UART не запущен", tag, (unsigned long)wanted);
        return;
    }
    const uint32_t diff = (wanted > (uint32_t)actual) ? (wanted - (uint32_t)actual)
                                                      : ((uint32_t)actual - wanted);
    const uint32_t permille = (diff * 1000u) / (wanted ? wanted : 1u);
    crsf_log_printf(&g_log, "%s: запрошено %lu бод, фактически %d бод (расхождение %u.%u %%)",
                    tag, (unsigned long)wanted, actual, (unsigned)(permille / 10u),
                    (unsigned)(permille % 10u));
    if (permille > 30u) {
        crsf_log_printf(&g_log, "  ВНИМАНИЕ: %s: расхождение больше 3 %% — приёмник не удержит кадр", tag);
    }
}

static void print_stats(const char *tag)
{
    const crsf_bridge_stats_t *st = crsf_bridge_stats(&g_bridge);
    if (st == NULL) {
        return;
    }
    /* Загрузка линий в процентах от скорости UART (8N1: бод/10 = байт/с). */
    const uint32_t cap_in = g_baud_in / 10u;
    const uint32_t cap_out = g_baud_out / 10u;
    const uint32_t pct_in = cap_in ? (g_util_in_bps * 100u) / cap_in : 0u;
    const uint32_t pct_out = cap_out ? (g_util_out_bps * 100u) / cap_out : 0u;

    crsf_log_printf(&g_log, "%s ok=%lu bad=%lu merged=%lu dropped=%lu in=%lu out=%lu q=%u/%lu uart_ovf=%lu disc=%lu",
                  tag,
                  (unsigned long)st->frames_ok,
                  (unsigned long)st->frames_bad,
                  (unsigned long)st->frames_coalesced,
                  (unsigned long)st->frames_dropped,
                  (unsigned long)st->bytes_in,
                  (unsigned long)st->bytes_out,
                  (unsigned)crsf_bridge_pending(&g_bridge),
                  (unsigned long)CRSF_BAUD_QUEUE_SLOTS,
                  (unsigned long)g_uart_overruns,
                  (unsigned long)g_bytes_discarded);
    crsf_log_printf(&g_log, "  линия: вход %lu Б/с (%lu%% от %lu бод), выход %lu Б/с (%lu%% от %lu бод)",
                    (unsigned long)g_util_in_bps, (unsigned long)pct_in, (unsigned long)g_baud_in,
                    (unsigned long)g_util_out_bps, (unsigned long)pct_out, (unsigned long)g_baud_out);
    if (pct_out >= 90u) {
        crsf_log_printf(&g_log, "  ВНИМАНИЕ: выход занят на %lu%% — скорости не хватает, кадры теряются", (unsigned long)pct_out);
    }
}

/* Пересчитать загрузку линий раз в секунду.
 *
 * bytes/s и процент от скорости UART: UART 8N1 = 10 бит на байт, поэтому
 * байт/с = бод / 10. Если процент на выходе близок к 100, скорости не
 * хватает: кадры будут вытесняться из очереди (счётчик dropped). */
static void update_line_utilization(void)
{
    const uint32_t now = millis();
    if (g_util_window_ms == 0u) {
        g_util_window_ms = now;
        const crsf_bridge_stats_t *st0 = crsf_bridge_stats(&g_bridge);
        if (st0 != NULL) {
            g_util_last_in = st0->bytes_in;
            g_util_last_out = st0->bytes_out;
        }
        return;
    }

    const uint32_t dt = now - g_util_window_ms;
    if (dt < 1000u) {
        return;
    }

    const crsf_bridge_stats_t *st = crsf_bridge_stats(&g_bridge);
    if (st != NULL) {
        const uint32_t d_in = st->bytes_in - g_util_last_in;
        const uint32_t d_out = st->bytes_out - g_util_last_out;
        /* d * 1000 / dt — байт/с; чтобы не потерять точность при dt != 1000,
         * считаем в тысячных байта/с и потом округляем. */
        g_util_in_bps = (uint32_t)(((uint64_t)d_in * 1000u) / dt);
        g_util_out_bps = (uint32_t)(((uint64_t)d_out * 1000u) / dt);
        g_util_last_in = st->bytes_in;
        g_util_last_out = st->bytes_out;
    }
    g_util_window_ms = now;
}

/* Отправить накопленные строки в USB-консоль, не блокируя конвейер.
 *
 * Буфер CDC — 64 байта, строка статистики длиннее. Serial.write в таком
 * случае блокируется, пока хост не прочитает данные (до секунды), а всё это
 * время мост не читает CRSF — теряются байты приёмника. Поэтому отдаём
 * только то, что влезло в свободное место буфера, остальное — в следующий
 * вызов loop(). */
static void flush_log(void)
{
    /* Хост не подключён — не тратим время и не теряем строки: они остаются
     * в очереди, и host, когда подключится, получит свежие данные. */
    if (!Serial) {
        return;
    }

    int space = Serial.availableForWrite();
    if (space <= 0) {
        return;   /* хост не читает — ждём, ничего не блокируя */
    }

    while (space > 0) {
        size_t len = 0u;
        const char *line = crsf_log_current(&g_log, &len);
        if (line == NULL) {
            /* Текущей строки нет — берём следующую из кольца. */
            if (!crsf_log_take(&g_log)) {
                return;   /* очередь пуста */
            }
            line = crsf_log_current(&g_log, &len);
            if (line == NULL) {
                return;
            }
        }

        if (len == 0u) {
            crsf_log_current_done(&g_log);
            continue;
        }

        /* Строка может быть длиннее буфера CDC (64 байта) — шлём частями. */
        size_t left = len - g_out_off;
        size_t chunk = (left > (size_t)space) ? (size_t)space : left;
        Serial.write((const uint8_t *)&line[g_out_off], chunk);
        g_out_off += chunk;
        space -= (int)chunk;

        if (g_out_off >= len) {
            crsf_log_current_done(&g_log);
            g_out_off = 0u;
            if (space <= 0) {
                return;
            }
            /* Перевод строки — отдельной порцией, если есть место. */
            Serial.write((const uint8_t *)"\n", 1u);
            space -= 1;
        }
    }
}

void setup()
{
    Serial.begin(CRSF_BAUD_LOG_BAUD);
    crsf_log_init(&g_log);

#if CRSF_BAUD_LED_ENABLE
    pinMode(CRSF_BAUD_LED_PIN, OUTPUT);
    digitalWrite(CRSF_BAUD_LED_PIN, LOW);
#endif

    crsf_bridge_init(&g_bridge, g_slots, (uint8_t)CRSF_BAUD_QUEUE_SLOTS);
    crsf_bridge_set_coalesce(&g_bridge, (uint8_t)(CRSF_BAUD_COALESCE ? 1 : 0));
#if CRSF_BAUD_ECHO_ENABLE
    crsf_bridge_init(&g_echo_bridge, g_echo_slots, (uint8_t)CRSF_BAUD_QUEUE_SLOTS);
    crsf_bridge_set_coalesce(&g_echo_bridge, (uint8_t)(CRSF_BAUD_COALESCE ? 1 : 0));
#endif
    crsf_cli_init(&g_cli);

    /* Пины: setPinout(tx, rx) — порядок именно такой. */
    if (!s_in.setPinout(CRSF_BAUD_IN_TX_PIN, CRSF_BAUD_IN_RX_PIN)) {
        crsf_log_printf(&g_log, "ВНИМАНИЕ: не удалось назначить пины входного UART");
    }
    if (!s_out.setPinout(CRSF_BAUD_OUT_TX_PIN, CRSF_BAUD_OUT_RX_PIN)) {
        crsf_log_printf(&g_log, "ВНИМАНИЕ: не удалось назначить пины выходного UART");
    }
    /* Размер FIFO приёма задаётся до begin(): на 420 кбод стандартных
     * 32 байт не хватает, пока отправка кадра в полётник блокирует. */
    s_in.setFIFOSize(CRSF_BAUD_RX_FIFO_SIZE);
    s_out.setFIFOSize(CRSF_BAUD_RX_FIFO_SIZE);

    s_in.begin(CRSF_BAUD_IN);
    s_out.begin(CRSF_BAUD_OUT);

    crsf_log_printf(&g_log, "picoCRSFbaudRedicer: in=%lu бод (UART%u), out=%lu бод (UART%u), слотов=%u",
                    (unsigned long)g_baud_in, (unsigned)CRSF_BAUD_IN_UART,
                    (unsigned long)g_baud_out, (unsigned)CRSF_BAUD_OUT_UART,
                    (unsigned)CRSF_BAUD_QUEUE_SLOTS);
    print_baud_pair("вход", g_baud_in, actual_baud_of(s_in, 0));
    print_baud_pair("выход", g_baud_out, actual_baud_of(s_out, 0));
    crsf_log_printf(&g_log, "ждём CRSF от приёмника...");
    crsf_log_printf(&g_log, "подсказка: скорость на выходе должна ТОЧНО совпадать с настройкой полётника");
}

static void print_help(void)
{
    crsf_log_printf(&g_log, "команды: help | status | stats | baud_in <n> | baud_out <n>");
    crsf_log_printf(&g_log, "  baud_in/baud_out меняют скорость на лету (n = бод, например 420000)");
}

static void apply_baud_in(uint32_t baud)
{
    s_in.end();
    s_in.setPinout(CRSF_BAUD_IN_TX_PIN, CRSF_BAUD_IN_RX_PIN);
    s_in.setFIFOSize(CRSF_BAUD_RX_FIFO_SIZE);
    s_in.begin(baud);
    g_baud_in = baud;
}

static void apply_baud_out(uint32_t baud)
{
    s_out.end();
    s_out.setPinout(CRSF_BAUD_OUT_TX_PIN, CRSF_BAUD_OUT_RX_PIN);
    s_out.setFIFOSize(CRSF_BAUD_RX_FIFO_SIZE);
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
            crsf_log_printf(&g_log, "статус: in=%lu бод (UART%u), out=%lu бод (UART%u), слотов=%u, прореживание=%u",
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
            crsf_log_printf(&g_log, "входная скорость: %lu бод", (unsigned long)g_baud_in);
            print_baud_pair("вход", g_baud_in, actual_baud_of(s_in, 0));
            break;
        case CRSF_CLI_SET_BAUD_OUT:
            apply_baud_out(res.value);
            crsf_log_printf(&g_log, "выходная скорость: %lu бод", (unsigned long)g_baud_out);
            print_baud_pair("выход", g_baud_out, actual_baud_of(s_out, 0));
            crsf_log_printf(&g_log, "проверь, что полётник настроен ровно на эту скорость");
            break;
        case CRSF_CLI_UNKNOWN:
            crsf_log_printf(&g_log, "не понял команду (help — список)");
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
    flush_log();
    update_line_utilization();

    /* 1. Забираем байты от приёмника и скормим их мосту. */
    uint8_t chunk[CRSF_BAUD_RX_CHUNK];
    int received = 0;
    while (received < (int)sizeof(chunk) && s_in.available() > 0) {
        chunk[received++] = (uint8_t)s_in.read();
    }
    /* Переполнение FIFO приёма = потерянные байты на проводе. Считаем явно:
     * если uart_ovf растёт, мал буфер приёма или выход медленее входа. */
    if (s_in.overflow()) {
        g_uart_overruns++;
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

    /* 3. Обратный канал: полётник → приёмник.
     *
     * В односторонней схеме (приёмник без телеметрии, RX-сторона приёмника
     * не подключена) этого потока нет вообще: полётник ничего не шлёт.
     * Но даже тогда UART вычитываем — иначе байты копятся в его FIFO и он
     * переполняется. В двусторонней схеме кадры идут через отдельный мост:
     * проверяется CRC, кадры прореживаются по типу. Сырые байты вслепую не
     * пересылаем — иначе в приёмник уйдёт мусор, который он не разберёт.
     *
     * Скорости на двух сторонах не обязаны совпадать: полётник говорит на
     * CRSF_BAUD_OUT, приёмник принимает на CRSF_BAUD_IN. */
    int echo_received = 0;
    while (echo_received < (int)sizeof(chunk) && s_out.available() > 0) {
        chunk[echo_received++] = (uint8_t)s_out.read();
    }
    if (echo_received > 0) {
#if CRSF_BAUD_ECHO_ENABLE
        crsf_bridge_feed(&g_echo_bridge, chunk, (size_t)echo_received);
#else
        /* Обратный канал выключен: данные от полётника некуда деть. */
        g_bytes_discarded += (uint32_t)echo_received;
#endif
    }
#if CRSF_BAUD_ECHO_ENABLE
    /* Отдаём целые кадры обратно в приёмник. */
    uint8_t echo_frame[CRSF_BRIDGE_MAX_FRAME];
    uint8_t echo_frame_len = 0u;
    while (crsf_bridge_pop(&g_echo_bridge, echo_frame, &echo_frame_len)) {
        s_in.write(echo_frame, echo_frame_len);
    }
#endif

#if CRSF_BAUD_LOG_EVERY_FRAMES > 0
    /* 4. Периодическая статистика — в очередь лога, оттуда наружу
     *    порциями (flush_log), без блокировки конвейера. */
    const crsf_bridge_stats_t *st = crsf_bridge_stats(&g_bridge);
    if (st != NULL && st->frames_ok >= g_logged_frames + CRSF_BAUD_LOG_EVERY_FRAMES) {
        g_logged_frames = st->frames_ok;
        print_stats("crsf:");
    }
#endif
}
