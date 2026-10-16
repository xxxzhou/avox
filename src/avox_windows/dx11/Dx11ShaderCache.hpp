#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <chrono>
#include <mutex>
#include <string>
#include <unordered_map>

#include <d3dcompiler.h>
#include <wrl/client.h>

namespace avox {

// 原生(DX11)车道的着色器字节码缓存。
// 背景(实测): Dx11CSVideoRender / Dx11VideoYuv / Dx11Window 都用运行时
// D3DCompile 编译内联 HLSL 源码, 单次 ~95~110ms(fxc 前端 HLSL->DXBC 是纯 CPU
class Dx11ShaderCache {
 public:
  // 命中或编译成功返回常驻 blob(所有权在缓存, 调用方不要 Release);
  // 失败返回 nullptr, 若传入 errBlob 则带回编译器错误(所有权归调用方, 用后 Release)
  static ID3DBlob* get(const char* source, const char* entry, const char* target,
                       ID3DBlob** errBlob = nullptr) {
    if (!source || !entry || !target) {
      return nullptr;
    }
    const std::string key = makeKey(source, entry, target);
    State& st = state();
    std::lock_guard<std::mutex> lock(st.mtx);
    auto it = st.cache.find(key);
    if (it != st.cache.end()) {
      return it->second.Get();
    }
    ID3DBlob* blob = nullptr;
    ID3DBlob* err = nullptr;
    // 与各调用点原先的参数保持一致(flags 0 = 默认优化级别)
    auto t0 = std::chrono::steady_clock::now();
    HRESULT hr = D3DCompile(source, strlen(source), nullptr, nullptr, nullptr,
                            entry, target, 0, 0, &blob, &err);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::steady_clock::now() - t0).count();
    if (FAILED(hr)) {
      if (blob) {
        blob->Release();
      }
      if (errBlob) {
        *errBlob = err;
      } else if (err) {
        err->Release();
      }
      return nullptr;
    }
    // 成功路径偶带 warning, 与改动前一样不关心
    if (err) {
      err->Release();
    }
    st.cache.emplace(key, blob);
    fprintf(stderr, "Dx11ShaderCache D3DCompile %s %lldms\n", key.c_str(),
            (long long)ms);
    return blob;
  }

  // 已缓存条数(调试/测试用)
  static size_t size() {
    State& st = state();
    std::lock_guard<std::mutex> lock(st.mtx);
    return st.cache.size();
  }

 private:
  struct State {
    std::mutex mtx;
    std::unordered_map<std::string, Microsoft::WRL::ComPtr<ID3DBlob>> cache;
  };

  static State& state() {
    // 故意泄漏: 静态析构期 Release 会回调 d3dcompiler.dll, 退出顺序不保证
    static State* s = new State();
    return *s;
  }

  static std::string makeKey(const char* source, const char* entry,
                             const char* target) {
    // FNV-1a 64: 仅作缓存 key(非安全用途), 避免把整段源码存进 map
    uint64_t hash = 1469598103934665603ULL;
    for (const char* p = source; *p; ++p) {
      hash ^= (uint64_t)(uint8_t)*p;
      hash *= 1099511628211ULL;
    }
    char buf[24] = {};
    snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)hash);
    return std::string(entry) + '|' + target + '|' + buf;
  }
};

}
