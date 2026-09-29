#ifndef BL_SIGN_H
#define BL_SIGN_H
/* 镜像签名验签（protocol.md §5.11，ADR-020）：ECDSA P-256 对镜像 SHA-256 摘要。
   仅 BL_SIGN_EN=1 的支持包提供真实现；固件侧只验签，私钥永不下设备。 */
#include <stdint.h>
#include <stdbool.h>

/* sha = 镜像 SHA-256 摘要（32B）；sig = r‖s 定宽大端裸序（64B，protocol.md §5.11）。
   公钥来自部署侧本地头 bl_sign_pubkey_local.h（不入库，见 docs/dev 模板）。 */
bool bl_sign_verify_digest(const uint8_t sha[32], const uint8_t sig[64]);

#endif /* BL_SIGN_H */
