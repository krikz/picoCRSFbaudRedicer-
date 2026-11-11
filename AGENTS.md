# AGENTS.md

## Что это

Преобразователь скорости линии CRSF на Raspberry Pi Pico (RP2040): приёмник
Crossfire/ExpressLRS (400 кбод) → Pico → полётник (200 кбод). Содержимое кадров
не меняется, меняется только скорость линии.

## Правила

- **Протокол — по первоисточникам.** Формат CRSF, CRC и скорости берём из
  [TBS spec](https://github.com/tbs-fpv/tbs-crsf-spec/blob/main/crsf.md),
  [Betaflight crsf_protocol.h](https://github.com/betaflight/betaflight/blob/master/src/main/rx/crsf_protocol.h)
  и документации EdgeTX. Не из памяти.
- **CRC-8/DVB-S2**: poly 0xD5, init 0x00, refin/refout false, xorout 0x00,
  check для `"123456789"` = **0xBC** (полином 0xD5 — это не check-значение).
- **Логика моста не зависит от железа** (`firmware/src/crsf_baud_bridge.c`,
  `firmware/src/crsf_baud_cli.c`) — только C99, чтобы её можно было тестировать
  нативно. Всё, что связано с UART, живёт в `main.cpp`.
- **Никаких ключей и токенов** в репозитории.
- **Стиль** — по GOODWORKRINKZ/biba: PlatformIO с env'ами по таргетам,
  `G-`-наименование CI, нативные тесты без железа, README по-русски.

## Проверка перед коммитом

```bash
cd firmware
gcc -std=gnu11 -Wall -Wextra -Wpedantic -Werror \
    -DCRSF_BAUD_TEST_STANDALONE \
    -Iinclude -Isrc -Itest/test_support \
    test/test_crsf_baud_bridge/test_main.c \
    src/crsf_baud_bridge.c src/crsf.c \
    -o /tmp/crsf_baud_test && /tmp/crsf_baud_test
```

Нативные тесты должны проходить целиком, без предупреждений компилятора.

## Известные ограничения

- Мост работает по кадрам CRSF: поток, который не CRSF, не пройдёт.
- Входная скорость выше выходной: при непрерывном потоке очередь переполняется
  и вытесняются старые кадры (`dropped` в логах).
- Обратный канал (полётник → приёмник) по умолчанию выключен.
