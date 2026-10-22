/**
 * @file linkit_crc16.c
 * @brief LinkIt 通用 CRC16-CCITT-FALSE 实现
 *
 * @details
 * v2.1 通用数据帧使用该 CRC 覆盖帧头和 payload，用于发现串口噪声、
 * 半包错位和链路切换瞬间的异常字节。
 */

#include "linkit_crc16.h"

/**
 * @brief 计算 CRC16-CCITT-FALSE。
 * @param data 输入数据指针；为 NULL 时返回初始值 0xFFFF。
 * @param len 输入数据长度。
 * @return 16 位 CRC 结果。
 */
uint16_t linkit_crc16_ccitt_false(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFFU;

    if (data == 0) {
        return crc;
    }

    for (size_t i = 0; i < len; ++i) {
        crc ^= (uint16_t)data[i] << 8;
        for (uint8_t bit = 0; bit < 8; ++bit) {
            if ((crc & 0x8000U) != 0U) {
                crc = (uint16_t)((crc << 1) ^ 0x1021U);
            } else {
                crc = (uint16_t)(crc << 1);
            }
        }
    }

    return crc;
}
