#include "bl_sha256.h"

/* SHA-256（FIPS 180-4）流式实现，ADR-020。
   参考测试向量（NIST CSRC）：
   - sha256("")       = e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855
   - sha256("abc")    = ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad
   - sha256("a"*1000000) 块更新路径由 tools/sign_image.py 对拍覆盖 */

static const uint32_t k_round[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
    0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
    0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
    0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
};

static uint32_t ror32(uint32_t v, uint32_t n)
{
    return (v >> n) | (v << (32u - n));
}

static void sha256_block(uint32_t state[8], const uint8_t block[64])
{
    uint32_t w[64];
    uint32_t a, b, c, d, e, f, g, h, t1, t2;
    uint32_t i;

    for (i = 0u; i < 16u; i++) {
        w[i] = ((uint32_t)block[i * 4u] << 24) |
               ((uint32_t)block[i * 4u + 1u] << 16) |
               ((uint32_t)block[i * 4u + 2u] << 8) |
               (uint32_t)block[i * 4u + 3u];
    }
    for (i = 16u; i < 64u; i++) {
        uint32_t s0 = ror32(w[i - 15u], 7u) ^ ror32(w[i - 15u], 18u) ^ (w[i - 15u] >> 3u);
        uint32_t s1 = ror32(w[i - 2u], 17u) ^ ror32(w[i - 2u], 19u) ^ (w[i - 2u] >> 10u);
        w[i] = w[i - 16u] + s0 + w[i - 7u] + s1;
    }

    a = state[0]; b = state[1]; c = state[2]; d = state[3];
    e = state[4]; f = state[5]; g = state[6]; h = state[7];

    for (i = 0u; i < 64u; i++) {
        uint32_t s1 = ror32(e, 6u) ^ ror32(e, 11u) ^ ror32(e, 25u);
        uint32_t ch = (e & f) ^ ((~e) & g);
        uint32_t s0 = ror32(a, 2u) ^ ror32(a, 13u) ^ ror32(a, 22u);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        t1 = h + s1 + ch + k_round[i] + w[i];
        t2 = s0 + maj;
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }

    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
    state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}

void bl_sha256_init(bl_sha256_t *ctx)
{
    ctx->state[0] = 0x6a09e667u;
    ctx->state[1] = 0xbb67ae85u;
    ctx->state[2] = 0x3c6ef372u;
    ctx->state[3] = 0xa54ff53au;
    ctx->state[4] = 0x510e527fu;
    ctx->state[5] = 0x9b05688cu;
    ctx->state[6] = 0x1f83d9abu;
    ctx->state[7] = 0x5be0cd19u;
    ctx->bitlen = 0u;
    ctx->buflen = 0u;
}

void bl_sha256_update(bl_sha256_t *ctx, const uint8_t *data, uint32_t len)
{
    uint32_t n;

    ctx->bitlen += (uint64_t)len * 8u;
    if (ctx->buflen > 0u) {
        n = 64u - ctx->buflen;
        if (n > len) {
            n = len;
        }
        for (uint32_t i = 0u; i < n; i++) {
            ctx->buf[ctx->buflen + i] = data[i];
        }
        ctx->buflen += n;
        data += n;
        len -= n;
        if (ctx->buflen == 64u) {
            sha256_block(ctx->state, ctx->buf);
            ctx->buflen = 0u;
        }
    }
    while (len >= 64u) {
        sha256_block(ctx->state, data);
        data += 64u;
        len -= 64u;
    }
    if (len > 0u) {
        for (uint32_t i = 0u; i < len; i++) {
            ctx->buf[i] = data[i];
        }
        ctx->buflen = len;
    }
}

void bl_sha256_final(bl_sha256_t *ctx, uint8_t out[32])
{
    uint64_t bitlen = ctx->bitlen;
    uint32_t i;

    ctx->buf[ctx->buflen++] = 0x80u;
    if (ctx->buflen > 56u) {
        while (ctx->buflen < 64u) {
            ctx->buf[ctx->buflen++] = 0u;
        }
        sha256_block(ctx->state, ctx->buf);
        ctx->buflen = 0u;
    }
    while (ctx->buflen < 56u) {
        ctx->buf[ctx->buflen++] = 0u;
    }
    for (i = 0u; i < 8u; i++) {
        ctx->buf[56u + i] = (uint8_t)(bitlen >> (56u - 8u * i));
    }
    sha256_block(ctx->state, ctx->buf);

    for (i = 0u; i < 8u; i++) {
        out[i * 4u] = (uint8_t)(ctx->state[i] >> 24);
        out[i * 4u + 1u] = (uint8_t)(ctx->state[i] >> 16);
        out[i * 4u + 2u] = (uint8_t)(ctx->state[i] >> 8);
        out[i * 4u + 3u] = (uint8_t)ctx->state[i];
    }
}

void bl_sha256(const uint8_t *data, uint32_t len, uint8_t out[32])
{
    bl_sha256_t ctx;
    bl_sha256_init(&ctx);
    bl_sha256_update(&ctx, data, len);
    bl_sha256_final(&ctx, out);
}
