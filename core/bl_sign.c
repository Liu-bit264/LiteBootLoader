#include "bl_sign.h"
#include "board_config.h"

#if BL_SIGN_EN

/* 开启必警告（用户要求，ADR-020）：AC5 无 #pragma message（实测 #161-D
   unrecognized），也不支持 #warning（1215 号错误）——用未引用静态数组触发
   默认 #177-D 警告，把启用提示文本带上 UV4 构建日志（该变体构建退出码 1
   属预期，0 警告基线对签名变体让位） */
#if defined(__CC_ARM)
static const char bl_sign_enabled_notice[] =
    "BL_SIGN_EN=1: signature verify enabled - confirm bl_sign_pubkey_local.h"
    " holds the production public key; rotate keys before production (ADR-020)";
#else
    #warning "BL_SIGN_EN=1: signature verify enabled; confirm bl_sign_pubkey_local.h holds the production public key (ADR-020)"
#endif

/* 公钥独立本地文件（用户要求：任何形态不入 git，含测试公钥）：
   由 tools/sign_image.py --keygen 生成到 port/<chip>/bl_sign_pubkey_local.h，
   或按 docs/dev/bl_sign_pubkey_local.template.h 手填。缺失时本文件 #error。 */
#include "bl_sign_pubkey_local.h"

#include "uECC.h"

static const uint8_t s_pubkey[64] = { BL_SIGN_PUBKEY_BYTES };

bool bl_sign_verify_digest(const uint8_t sha[32], const uint8_t sig[64])
{
    /* uECC_VLI_NATIVE_LITTLE_ENDIAN=0（默认）：key/sig 字节序为标准大端，
       与协议 §5.11 及主机侧 cryptography 输出一致，无需转换 */
    return uECC_verify(s_pubkey, sha, 32u, sig, uECC_secp256r1()) == 1;
}

#else

/* BL_SIGN_EN=0：能力关闭。占位实现保证翻译单元非空；0x11 命令处理同样被
   BL_SIGN_EN 拦截（按未知命令回 STATE_ERROR），运行时不会到达此处 */
bool bl_sign_verify_digest(const uint8_t sha[32], const uint8_t sig[64])
{
    (void)sha;
    (void)sig;
    return false;
}

#endif /* BL_SIGN_EN */
