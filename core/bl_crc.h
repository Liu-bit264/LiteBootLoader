#ifndef BL_CRC_H
#define BL_CRC_H
/* CRC 参数固化于 design.md ADR-001/002；标准校验值见各实现 */
#include <stdint.h>
#include <stddef.h>

#define BL_CRC16_INIT      0xFFFFu
#define BL_CRC32_INIT      0xFFFFFFFFu

uint16_t bl_crc16_modbus(const uint8_t *data, uint32_t len);          /* "123456789"->0x4B37 */
uint16_t bl_crc16_update(uint16_t crc, const uint8_t *data, uint32_t len);
uint32_t bl_crc32_iso_hdlc(const uint8_t *data, uint32_t len);        /* "123456789"->0xCBF43926 */
uint32_t bl_crc32_update(uint32_t crc, const uint8_t *data, uint32_t len); /* 链式：init BL_CRC32_INIT */

#endif /* BL_CRC_H */
