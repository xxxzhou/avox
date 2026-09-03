// httplib + OpenSSL + avox.dll 验证测试
// 注意: 不要在 exe 中直接 #include "httplib.h", 否则 exe 和 avox.dll 各有一份
// httplib 静态变量, OpenSSL 重复初始化会 crash。只通过 avox.dll 的 C 导出测试。
#include <iostream>

#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#endif

#include "avox_agent/transport/IHttpTransport.hpp"   // vaildHttps (经 avox.dll 的 C 导出)

using namespace avox;

void testVaildHttps(const char* url) {
  std::cout << "vaildHttps(\"" << url << "\")..." << std::endl;
  bool ok = vaildHttps(url);
  std::cout << "  result: " << (ok ? "OK" : "FAIL") << std::endl;
}

int main() {
  std::cout << "=== avox.dll SSL test ===" << std::endl;

  // 测试1: vaildHttps 验证 (通过 avox.dll 内的 httplib + OpenSSL)  
  testVaildHttps("http://127.0.0.1");
  testVaildHttps("https://127.0.0.1");
  testVaildHttps("https://www.baidu.com");
  testVaildHttps("https://qianfan.baidubce.com/v2/coding");

  return 0;
}
