#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>

#include <d3dcompiler.h>
#include <wrl/client.h>

namespace avox {

// 原生(DX11)车道的着色器字节码缓存。
//
// 背景(实测): Dx11CSVideoRender / Dx11VideoYuv / Dx11Window 都用运行时
// D3DCompile 编译内联 HLSL 源码, 单次 ~95~110ms(fxc 前端 HLSL->DXBC 是纯 CPU
// 编译, 与驱动无关); 而 CreateComputeShader 只要 0.3~0.8ms。原生车道每次图重建
// 都会重进 createProgram —— enableYuvOut/disableYuvOut(开始/停止录制或 YUV 输出)、
// setSurface、尺寸或像素格式(NV12<->P010)变化、以及同进程新建 player —— 于是每次
// 都白付这一百毫秒(表现为起播/开录瞬间掉帧)。
//
// 本缓存只缓存"字节码"(DXBC, 与设备无关), 因此:
//   - 跨 device / 跨 player 复用安全, 命中后只剩 CreateXxxShader 的亚毫秒开销;
//   - 着色器对象(ID3D11ComputeShader 等)是设备相关的, 仍由各调用点按 device 自建;
//   - 编译失败不缓存, 保证错误每次可见、可复现。
//
// 线程安全: 同一 (源码, 入口, target) 并发首建只编译一次(锁内双击检查)。
// 生命周期: 单条 10~30KB, 进程内常驻; 刻意不做静态析构(见 state()), 避免退出阶段
// 回调已卸载的 d3dcompiler.dll。源码改动会因 key 变化自然失效。
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
    HRESULT hr = D3DCompile(source, strlen(source), nullptr, nullptr, nullptr,
                            entry, target, 0, 0, &blob, &err);
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
