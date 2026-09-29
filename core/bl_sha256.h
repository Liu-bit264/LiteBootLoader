#ifndef BL_SHA256_H
#define BL_SHA256_H
/* 流式 SHA-256（FIPS 180-4，ADR-020 验签用）：无动态内存，块间喂狗由调用方安排 */
#include <stdint.h>
#include <stddef.h>

typedef struct {
    uint32_t state[8];
    uint64_t bitlen;    /* 已处理总位数 */
    uint8_t  buf[64];   /* 不满一块的残留 */
    uint32_t buflen;
} bl_sha256_t;

void bl_sha256_init(bl_sha256_t *ctx);
void bl_sha256_update(bl_sha256_t *ctx, const uint8_t *data, uint32_t len);
void bl_sha256_final(bl_sha256_t *ctx, uint8_t out[32]);   /* 大端摘要 */

/* 一次性便捷封装 */
void bl_sha256(const uint8_t *data, uint32_t len, uint8_t out[32]);

#endif /* BL_SHA256_H */
