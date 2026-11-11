# firmware

Прошивка преобразователя скорости CRSF для RP2040 (Raspberry Pi Pico).

## Структура

```
firmware/
├── include/
│   ├── crsf_baud_config.h     все настройки (переопределяются через -D)
│   └── crsf_baud_bridge.h     API портируемого моста
├── src/
│   ├── crsf.[ch]              CRC8 DVB-S2 и разбор кадра (из biba)
│   ├── crsf_baud_bridge.c     мост: сборка кадров, проверка CRC, очередь, прореживание
│   ├── crsf_baud_cli.c        разбор команд USB-консоли
│   └── main.cpp               UART на Pico, светодиод, логи, консоль
├── targets/
│   └── RP2040_CRSF_BAUD_REDUCER/
│       ├── target.h            описание платы
│       ├── target_config.h     настройки таргета
│       └── target.md           распиновка и сборка
└── test/
    ├── test_support/            Unity-шим (standalone-режим для gcc)
    ├── test_crsf_baud_bridge/   нативные тесты логики моста (15)
    └── test_crsf_baud_cli/      нативные тесты разбора команд (10)
```

## Сборка

```bash
pio run -e rp2040_crsf_baud_reducer           # .pio/build/rp2040_crsf_baud_reducer/
pio run -e rp2040_crsf_baud_reducer -t upload # прошивка через picotool
```

Требуется [PlatformIO](https://platformio.org/). Платформа — форк
`maxgerhardt/platform-raspberrypi`; `framework = arduino`, `board = rpipico`.

## Тесты

Нативные тесты логики моста (без PlatformIO и без железа):

```bash
cd firmware
gcc -std=gnu11 -Wall -Wextra -Wpedantic -Werror \
    -DCRSF_BAUD_TEST_STANDALONE \
    -Iinclude -Isrc -Itest/test_support \
    test/test_crsf_baud_bridge/test_main.c \
    src/crsf_baud_bridge.c src/crsf.c \
    -o /tmp/crsf_baud_test && /tmp/crsf_baud_test
```

То же через PlatformIO:

```bash
pio test -e native_test
```

Что покрыто: контрольное значение CRC8, побайтовый round-trip кадра, отбраковка
битого CRC и недопустимой длины, ресинхронизация после шума и битого кадра,
таймаут неполного кадра, переполнение очереди с вытеснением старого кадра,
прореживание по типу кадра, счётчики статистики, разбор команд консоли.

Тест консоли — отдельно:

```bash
gcc -std=gnu11 -Wall -Wextra -Wpedantic -Werror -DCRSF_BAUD_TEST_STANDALONE \
    -Iinclude -Isrc -Itest/test_support \
    test/test_crsf_baud_cli/test_main.c src/crsf_baud_cli.c \
    -o /tmp/crsf_baud_cli_test && /tmp/crsf_baud_cli_test
```

## Команды USB-консоли

```
help | status | stats | baud_in <n> | baud_out <n>
```

Скорости приёмника и полётника меняются на лету — пересборка не нужна.

## Логи на устройстве

USB-консоль, 115200 бод:

```
picoCRSFbaudRedicer: in=420000 бод (UART2), out=200000 бод (UART1), слотов=8
ждём CRSF от приёмника...
crsf: ok=100 bad=0 dropped=0 in=2400 out=2400 q=0/32
```
