#ifndef CRSF_BAUD_CONFIG_H
#define CRSF_BAUD_CONFIG_H

/* Настройки преобразователя скорости CRSF для RP2040 (Raspberry Pi Pico).
 *
 * Любое значение переопределяется через -D — из platformio.ini или из
 * targets/<TARGET>/target_config.h. Ничего не зашито в коде намертво.
 *
 * Схема работы:
 *   приёмник Crossfire/ELRS --(CRSF, CRSF_BAUD_IN)--> [Pico] --> (CRSF, CRSF_BAUD_OUT) --> полётник
 *
 * Скорость на выходе ниже скорости на входе — это нормально: CRSF идёт
 * короткими пачками, и частота кадров от этого не меняется.
 */

/* --- Скорости UART (бод) ---------------------------------------------- */
/* Приёмник: 420000 — ExpressLRS (это же значение в biba, где приёмник
 *           точно работает: BIBA_CRSF_BAUD = 420000).
 *           Прочие варианты: 400000 — базовый CRSF/TBS Rev7, 416666 — CRSF Rev10. */
#ifndef CRSF_BAUD_IN
#  define CRSF_BAUD_IN                 420000UL
#endif
/* Полётник: скорость, которую полётник ждёт на входе CRSF.
 * 200000 — как настроено на стороне полётника.
 * ВНИМАНИЕ: 200000 НЕ входит в стандартные списки CRSF-скоростей
 * (TBS Rev7 — 400000, Rev10 — 416666, ELRS — 420000; в EdgeTX список
 * скоростей модуля: 115200/400000/921600/1870000/3750000/5250000).
 * Если полётник на самом деле ждёт стандартную — поставь 400000.
 * Скорость можно сменить на лету: baud_in N / baud_out N в USB-консоли. */
#ifndef CRSF_BAUD_OUT
#  define CRSF_BAUD_OUT                200000UL
#endif

/* --- UART -------------------------------------------------------------- */
/* 1 = Serial1 (UART0), 2 = Serial2 (UART1). Конкретные пины — ниже;
 * GP0/GP1 на Pico заняты USB, поэтому по умолчанию CRSF идёт по GP4/GP5 и GP8/GP9. */
#ifndef CRSF_BAUD_IN_UART
#  define CRSF_BAUD_IN_UART             2
#endif
#ifndef CRSF_BAUD_OUT_UART
#  define CRSF_BAUD_OUT_UART            1
#endif
#if CRSF_BAUD_IN_UART == CRSF_BAUD_OUT_UART
#  error "CRSF_BAUD_IN_UART и CRSF_BAUD_OUT_UART должны быть разными"
#endif

/* --- Пины (RP2040 / Pico) -------------------------------------------- */
/* IN:  приём CRSF от приёмника (RX) и необязательный TX обратно в приёмник.
 * OUT: передача CRSF в полётник (TX) и необязательный RX от полётника. */
/* Распиновка по умолчанию. GP0/GP1 на Pico заняты USB (D+/D-) — там же по
 * умолчанию сидит Serial1 в варианте rpipico этого ядра, поэтому линии CRSF
 * уводим на GP4/GP5 и GP8/GP9. */
#ifndef CRSF_BAUD_IN_RX_PIN
#  define CRSF_BAUD_IN_RX_PIN            9   /* GP9 — приём CRSF от приёмника */
#endif
#ifndef CRSF_BAUD_IN_TX_PIN
#  define CRSF_BAUD_IN_TX_PIN            8   /* GP8 */
#endif
#ifndef CRSF_BAUD_OUT_TX_PIN
#  define CRSF_BAUD_OUT_TX_PIN           4   /* GP4 — передача CRSF в полётник */
#endif
#ifndef CRSF_BAUD_OUT_RX_PIN
#  define CRSF_BAUD_OUT_RX_PIN           5   /* GP5 */
#endif

/* Обратный канал (телеметрия полётника → приёмнику). По умолчанию выключен:
 * базовая схема «приёмник → полётник» односторонняя. */
#ifndef CRSF_BAUD_ECHO_ENABLE
#  define CRSF_BAUD_ECHO_ENABLE         0
#endif

/* --- Буфер ------------------------------------------------------------- */
/* Очередь целых CRSF-кадров. Слот = 64 байта (максимальная длина кадра в CRSF). */
#ifndef CRSF_BAUD_QUEUE_SLOTS
#  define CRSF_BAUD_QUEUE_SLOTS         8u
#endif
/* Прореживание: 1 — новый кадр заменяет в очереди более старый кадр того же
 * типа (в очереди всегда самая свежая команда RC). 0 — только вытеснение
 * самого старого при переполнении. */
#ifndef CRSF_BAUD_COALESCE
#  define CRSF_BAUD_COALESCE           1
#endif
/* Таймаут неполного кадра (мс). CRSF-кадры идут пачками с интервалом ~10 мс,
 * поэтому 20 мс — с запасом. По истечении таймаута незавершённый кадр
 * отбрасывается (иначе ложный sync-байт в шуме блокировал бы линию). */
#ifndef CRSF_BAUD_FRAME_TIMEOUT_MS
#  define CRSF_BAUD_FRAME_TIMEOUT_MS     20u
#endif
/* Сколько байт забирать из приёмника за один проход loop(). */
#ifndef CRSF_BAUD_RX_CHUNK
#  define CRSF_BAUD_RX_CHUNK            64u
#endif

/* --- Логи и индикация --------------------------------------------------- */
#ifndef CRSF_BAUD_LOG_BAUD
#  define CRSF_BAUD_LOG_BAUD             115200UL
#endif
/* Как часто печатать статистику (кадров). 0 = не печатать. */
#ifndef CRSF_BAUD_LOG_EVERY_FRAMES
#  define CRSF_BAUD_LOG_EVERY_FRAMES    100u
#endif
#ifndef CRSF_BAUD_LED_ENABLE
#  define CRSF_BAUD_LED_ENABLE           1
#endif
#ifndef CRSF_BAUD_LED_PIN
#  define CRSF_BAUD_LED_PIN              25u
#endif

#endif /* CRSF_BAUD_CONFIG_H */
