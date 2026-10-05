// iOS 静态核 BoringSSL 兼容面 (10/2): webrtc 归档的 BoringSSL 砍掉了 OpenSSL 的
// ctrl 兼容层与 get1 新名, httplib 恰好引用这三处 — 本单元按 BoringSSL 真函数
// 转发补齐。动态插件平台(mac 等)整文件空转(PLUGIN_BUILDING), 不与各端重复定义。
#if AVOX_ENABLE_STATIC && !defined(AVOX_PLUGIN_BUILDING)

#include <openssl/ssl.h>

// 两套头都读不顺: BoringSSL 头把 OpenSSL ctrl 宏钉成 doesnt_exist 雷(ssl.h
// 5995-6036, 本文件恰要补的就是 ctrl 缺口), OpenSSL 3.x 头又把 setter 做成
// SSL_ctrl 函数式宏(转发即自递归)。统一解干净后按 OpenSSL ctrl 层数值手工钉,
// 转发到 BoringSSL 真函数(webrtc 归档内, nm 实证)。
#undef SSL_CTRL_SET_TLSEXT_HOSTNAME
#undef SSL_CTRL_SET_MIN_PROTO_VERSION
#undef SSL_CTRL_SET_MAX_PROTO_VERSION
#undef SSL_CTRL_OPTIONS
#undef SSL_CTRL_CLEAR_OPTIONS
#define SSL_CTRL_SET_TLSEXT_HOSTNAME 55
#define SSL_CTRL_SET_MIN_PROTO_VERSION 123
#define SSL_CTRL_SET_MAX_PROTO_VERSION 124
#define SSL_CTRL_OPTIONS 32
#define SSL_CTRL_CLEAR_OPTIONS 33

#include <cstdint>

// OpenSSL 3.x 头把这些 setter 做成 SSL_ctrl 宏, BoringSSL 是真函数 —
// 解宏后手工声明 BoringSSL 原型, 否则转发即自递归。
// 例外: set_options/clear_options 在 3.x 头是真声明(uint64_t 版), 直接用头的,
// BoringSSL 侧 uint32_t 实现按 AAPCS64 低 32 位读, 返回值掩 32 位。
#undef SSL_set_tlsext_host_name
#undef SSL_set_min_proto_version
#undef SSL_set_max_proto_version
#undef SSL_CTX_set_min_proto_version
#undef SSL_CTX_set_max_proto_version

extern "C" {

// BoringSSL 真函数(webrtc 归档内, nm 实证), 手工声明
int SSL_set_tlsext_host_name(SSL *ssl, const char *hostname);
int SSL_set_min_proto_version(SSL *ssl, uint16_t version);
int SSL_set_max_proto_version(SSL *ssl, uint16_t version);
int SSL_CTX_set_min_proto_version(SSL_CTX *ctx, uint16_t version);
int SSL_CTX_set_max_proto_version(SSL_CTX *ctx, uint16_t version);
X509 *SSL_get_peer_certificate(const SSL *ssl);

long SSL_ctrl(SSL *ssl, int cmd, long larg, void *parg) {
  switch (cmd) {
  case SSL_CTRL_SET_TLSEXT_HOSTNAME:  // 55, httplib SNI
    return SSL_set_tlsext_host_name(ssl, static_cast<const char *>(parg));
  case SSL_CTRL_SET_MIN_PROTO_VERSION:
    return SSL_set_min_proto_version(ssl, static_cast<uint16_t>(larg));
  case SSL_CTRL_SET_MAX_PROTO_VERSION:
    return SSL_set_max_proto_version(ssl, static_cast<uint16_t>(larg));
  default:
    return 0;
  }
}

long SSL_CTX_ctrl(SSL_CTX *ctx, int cmd, long larg, void *parg) {
  (void)parg;
  switch (cmd) {
  case SSL_CTRL_SET_MIN_PROTO_VERSION:  // 123, httplib 强制 TLS1.2+
    return SSL_CTX_set_min_proto_version(ctx, static_cast<uint16_t>(larg));
  case SSL_CTRL_SET_MAX_PROTO_VERSION:
    return SSL_CTX_set_max_proto_version(ctx, static_cast<uint16_t>(larg));
  case SSL_CTRL_OPTIONS:  // 32, SSL_OP_NO_COMPRESSION 等
    return static_cast<long>(
        SSL_CTX_set_options(ctx, static_cast<uint64_t>(larg)) & 0xffffffffULL);
  case SSL_CTRL_CLEAR_OPTIONS:
    return static_cast<long>(
        SSL_CTX_clear_options(ctx, static_cast<uint64_t>(larg)) & 0xffffffffULL);
  default:
    return 0;
  }
}

// BoringSSL 只有旧名; OpenSSL 3.x 头声明新名, httplib 按新名引用
X509 *SSL_get1_peer_certificate(const SSL *ssl) {
  return SSL_get_peer_certificate(const_cast<SSL *>(ssl));
}

}  // extern "C"

#endif  // AVOX_ENABLE_STATIC && !AVOX_PLUGIN_BUILDING
