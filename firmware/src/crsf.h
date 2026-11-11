#ifndef CRSF_H
#define CRSF_H

/* Минимальный разбор CRSF (Crossfire Serial Format).
 *
 * Выдержан из проекта biba (GOODWORKRINKZ/biba, firmware/src/drivers/crsf.h):
 * тот же CRC8 и те же типы кадров, но имена без префикса biba_ — здесь
 * протокол общий, а не часть робота.
 *
 * Спецификация: https://github.com/tbs-fpv/tbs-crsf-spec/blob/main/crsf.md
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CRSF_SYNC_BYTE              0xC8u
#define CRSF_MAX_FRAME_SIZE         64u

#define CRSF_FRAMETYPE_RC_CHANNELS  0x16u
#define CRSF_FRAMETYPE_LINK_STATS   0x14u
#define CRSF_FRAMETYPE_BATTERY      0x08u
#define CRSF_FRAMETYPE_GPS          0x02u

/* CRC8 DVB-S2 (полином 0xD5) — тот же, что в CRSF. */
uint8_t crsf_crc8_dvb_s2(const uint8_t *data, size_t len);

/* CRC8 с накоплением: удобно считать CRC кадра по мере поступления байтов. */
uint8_t crsf_crc8_update(uint8_t crc, uint8_t byte);

/* Проверить целый кадр. Возвращает тип кадра или 0, если кадр битый. */
uint8_t crsf_parse_frame(const uint8_t *frame, size_t frame_len);

#ifdef __cplusplus
}
#endif

#endif /* CRSF_H */
