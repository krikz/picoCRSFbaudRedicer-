#include "crsf.h"

#include <string.h>

uint8_t crsf_crc8_update(uint8_t crc, uint8_t byte)
{
    crc ^= byte;
    for (unsigned bit = 0; bit < 8u; ++bit) {
        if (crc & 0x80u) {
            crc = (uint8_t)((crc << 1) ^ 0xD5u);
        } else {
            crc = (uint8_t)(crc << 1);
        }
    }
    return crc;
}

uint8_t crsf_crc8_dvb_s2(const uint8_t *data, size_t len)
{
    uint8_t crc = 0u;
    if (data == NULL) {
        return crc;
    }
    for (size_t i = 0; i < len; ++i) {
        crc = crsf_crc8_update(crc, data[i]);
    }
    return crc;
}

uint8_t crsf_parse_frame(const uint8_t *frame, size_t frame_len)
{
    if (frame == NULL || frame_len < 4u) {
        return 0u;
    }
    if (frame[0] != CRSF_SYNC_BYTE) {
        return 0u;
    }
    const uint8_t length = frame[1];
    if (length < 2u || (size_t)length + 2u != frame_len) {
        return 0u;
    }
    const uint8_t expected = crsf_crc8_dvb_s2(&frame[2], (size_t)length - 1u);
    if (expected != frame[frame_len - 1u]) {
        return 0u;
    }
    return frame[2];
}
