#ifndef LINKIT_CRC16_H
#define LINKIT_CRC16_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint16_t linkit_crc16_ccitt_false(const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif
