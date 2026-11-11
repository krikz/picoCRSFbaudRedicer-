# picoCRSFbaudReducer

[![Native Tests](https://github.com/krikz/picoCRSFbaudReducer-/actions/workflows/G-Build-Firmware-Native-Tests.yml/badge.svg?branch=main)](https://github.com/krikz/picoCRSFbaudReducer-/actions/workflows/G-Build-Firmware-Native-Tests.yml)
[![Firmware Build](https://github.com/krikz/picoCRSFbaudReducer-/actions/workflows/G-Build-Firmware.yml/badge.svg?branch=main)](https://github.com/krikz/picoCRSFbaudReducer-/actions/workflows/G-Build-Firmware.yml)

<p align="center">
   <pre>
   ┌──────────────┐   CRSF, 400 кбод   ┌───────────────┐   CRSF, 200 кбод   ┌──────────────┐
   │  Приёмник   │ ──────────────────► │  RP2040 Pico  │ ──────────────────► │  Полётник    │
   │ Crossfire / │   UART1 (GP4/GP5)  │  преобразова- │   UART0 (GP0/GP1)  │  (CRSF вход) │
   │ ExpressLRS  │                     │  тель скорости│                    │              │
   └──────────────┘                     └───────────────┘                    └──────────────┘
   </pre>
</p>

**picoCRSFbaudReducer** — преобразователь скорости линии CRSF на Raspberry Pi Pico (RP2040).
Он стоит между приёмником Crossfire/ExpressLRS и полётником: читает CRSF на одной скорости,
проверяет кадры и отдаёт их на другой. Никакой интерпретации протокола — байты кадра
передаются как есть, меняется только скорость линии.

## Задача

Приёмник отдаёт CRSF на **400 кбод** (у ExpressLRS — 420 кбод), а полётник настроен на
**200 кбод**. Скорость на приёме и на передаче должна отличаться, а содержимое кадров —
остаться тем же: CRC в CRSF считается по содержимому, к времени прихода байтов отношения не
имеет.

| Сторона | Скорость по умолчанию | Откуда взята цифра |
| --- | --- | --- |
| Приёмник → Pico | **400000** | базовый CRSF: 400 кбод 8N1, 3.3 V — [TBS CRSF spec](https://github.com/tbs-fpv/tbs-crsf-spec/blob/main/crsf.md) |
| Pico → полётник | **200000** | скорость, которую ждёт полётник; в EdgeTX выбирается в настройках модуля CRSF |

Все скорости, пины и параметры очереди задаются конфигом и переопределяются через `-D`
(см. [docs/configuration.md](docs/configuration.md)) — «400 кбод где-то» не значит «400000
навсегда»: у ELRS это 420000, у CRSF Rev10 — 416666.

## Что именно делает прошивка

1. **Собирает кадры.** В потоке ищется sync-байт `0xC8`, затем длина кадра, затем тело;
   CRC8 (DVB-S2, полином `0xD5`) считается на лету.
2. **Проверяет.** Кадр с неверной длиной или CRC отбрасывается и считается в
   `frames_bad` — мусор и обрывки в полётник не идут.
3. **Кладёт целые кадры в очередь** (по умолчанию 32 слота по 64 байта).
4. **Отдаёт целыми кадрами** на второй UART. Если очередь переполнилась, вытесняется
   **самый старый** кадр (`frames_dropped`): для RC свежая команда важнее запоздалой.

Логика моста — портируемый C без железа (`firmware/src/crsf_baud_bridge.c`), она покрыта
нативными тестами; `main.c` держит только UART и индикацию.

## Распиновка (Raspberry Pi Pico)

| Назначение | UART | GP | Примечание |
| --- | --- | --- | --- |
| CRSF от приёмника (вход) | UART1 RX | **GP5** | TX приёмника сюда |
| CRSF к полётнику (выход) | UART0 TX | **GP0** | вход CRSF полётника сюда |
| Обратный канал (опция) | UART0 RX | **GP1** | телеметрия полётника → приёмник, по умолчанию выключено |
| Обратный канал (опция) | UART1 TX | **GP4** | ответ приёмнику |
| Логи/диагностика | USB CDC | — | `Serial`, 115200 |
| Светодиод | GPIO | **GP25** | мигает при приёме кадров |

Пины можно переопределить (`CRSF_BAUD_RX_PIN` / `CRSF_BAUD_TX_PIN`).

**Уровни.** CRSF — это 3.3 V ([TBS spec](https://github.com/tbs-fpv/tbs-crsf-spec/blob/main/crsf.md),
[Betaflight `crsf_protocol.h`](https://github.com/betaflight/betaflight/blob/master/src/main/rx/crsf_protocol.h)):
3.3 V GPIO Pico подходит напрямую, **5 V на вход Pico подавать нельзя** — GPIO RP2040 не
терпит 5 V.

## Сборка и прошивка

```bash
cd firmware
pio run -e rp2040_crsf_baud_reducer          # сборка
pio run -e rp2040_crsf_baud_reducer -t upload # прошивка (picotool)
pio test -e native_test                        # нативные тесты
```

Требуется [PlatformIO](https://platformio.org/). Платформа — форк
`maxgerhardt/platform-raspberrypi`, `framework = arduino`, `board = rpipico`
(не `pico`: другой board id в этом форке тянет другую прошивку Arduino).

## Проверка без железа

Логика моста собирается обычным gcc и прогоняется нативными тестами:

```bash
cd firmware
gcc -std=gnu11 -Wall -Wextra -Wpedantic \
    -DCRSF_BAUD_TEST_STANDALONE \
    -Iinclude -Isrc -Itest/test_support \
    test/test_crsf_baud_bridge/test_main.c \
    src/crsf_baud_bridge.c src/crsf.c \
    -o /tmp/crsf_baud_test && /tmp/crsf_baud_test
```

## Документация

- [docs/wiring.md](docs/wiring.md) — схема, уровни, питание, что куда подключать.
- [docs/protocol.md](docs/protocol.md) — что именно пробрасывается и почему это безопасно.
- [docs/configuration.md](docs/configuration.md) — все параметры и как их менять.
- [firmware/README.md](firmware/README.md) — сборка, таргеты, тесты.

## Ограничения (честно)

- **Односторонний канал по умолчанию.** Телеметрия полётника обратно в приёмник не идёт,
  пока `CRSF_BAUD_ECHO_ENABLE = 0`. RC это не ломает, но приёмник не получит от полётника
  телеметрию.
- **Входящий поток быстрее исходящего.** 400 кбод на входе против 200 кбод на выходе: в
  установившемся режиме CRSF идёт короткими пачками, поэтому кадры успевают уходить, и
  частота кадров на полётнике не меняется (время на кадр в проводе растёт вдвое). Но если
  приёмник начнёт лить непрерывный поток, очередь переполнится и начнут вытесняться старые
  кадры — счётчик `frames_dropped` это покажет.
- **Только CRSF.** Поток, который не является CRSF (другой sync, другой протокол), не
  пройдёт: мост работает по кадрам, а не по байтам.
- **Кадр длиннее 64 байт** отбрасывается (ограничение спецификации CRSF).

## Источники

- [TBS Crossfire Serial Format (spec)](https://github.com/tbs-fpv/tbs-crsf-spec/blob/main/crsf.md) — кадр, CRC, скорости, уровни.
- [Betaflight `crsf_protocol.h`](https://github.com/betaflight/betaflight/blob/master/src/main/rx/crsf_protocol.h) — скорости CRSF Rev7/Rev10 и ELRS.
- [EdgeTX manual: Hardware](https://manual.edgetx.org/v2.11/color-radios/radio-settings/hardware) — выбор скорости модуля CRSF.
- [GOODWORKRINKZ/biba](https://github.com/GOODWORKRINKZ/biba) — стиль проекта и исходный CRSF-драйвер (`firmware/src/drivers/crsf.c`), оттуда же взята каркасная схема PlatformIO.
