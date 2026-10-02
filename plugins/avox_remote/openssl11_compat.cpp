// openssl11_compat — libsmb2 预编译件按 OpenSSL 1.1 API 编, 其中若干函数在
// 3.x 已改为宏(调用点符号仍按真函数引用), 需真实函数补齐 UND; 另有 Secure
// Transport 系符号由 Security.framework 提供(见插件 CMakeLists APPLE 分支)。
// Apple 专供: 其它平台编译为空, 不与真实 OpenSSL 函数冲突。
#if defined(__APPLE__)

#include <openssl/ssl.h>
#include <openssl/bio.h>

extern "C" {

#undef BIO_set_nbio
long BIO_set_nbio(BIO* b, long n) {
  return BIO_ctrl(b, BIO_C_SET_NBIO, n, nullptr);
}

#undef SSL_CTX_clear_mode
long SSL_CTX_clear_mode(SSL_CTX* ctx, long op) {
  return SSL_CTX_ctrl(ctx, SSL_CTRL_CLEAR_MODE, op, nullptr);
}

#undef SSL_clear_mode
long SSL_clear_mode(SSL* ssl, long op) {
  return SSL_ctrl(ssl, SSL_CTRL_CLEAR_MODE, op, nullptr);
}

#undef SSL_get_peer_certificate
X509* SSL_get_peer_certificate(const SSL* ssl) {
  return SSL_get1_peer_certificate(ssl);
}

#undef SSL_set_tlsext_host_name
int SSL_set_tlsext_host_name(SSL* ssl, const char* name) {
  return SSL_ctrl(ssl, SSL_CTRL_SET_TLSEXT_HOSTNAME,
                  TLSEXT_NAMETYPE_host_name, (void*)name);
}

}

#endif  // __APPLE__
