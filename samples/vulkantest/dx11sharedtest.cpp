// D3D11 共享输出通路验证 (模拟 Unity D3D11 后端):
// avox VK 管线 -> 底层自建 NT 共享纹理 -> 本进程第二个 D3D11 设备打开并复制读回
// 安全规矩: OpenSharedResource1 仅单次尝试, 失败即退出, 严禁重试
// 撕裂探针: 连续读回共享纹理, 与参考帧逐行哈希比对。读值分类:
//   unchanged(全行同参考) / full(新整帧) / partial(部分行同旧帧+部分不同)
// partial 即撕裂候选: 与随后整帧验证 —— 部分行 bit==旧帧 && 其余行 bit==新帧
// = 新旧帧时间混合(消费侧读到写一半的纹理); 行与新旧帧都不等 = 上游内容损坏。
// 候选抓三联 BMP(prev/cand/next) 供人工确认(静态背景的合法新帧行哈希形态
// 与撕裂相同, 最终判读看图: 撕裂帧有内容时间错位的横切缝)。
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include <d3d11.h>
#include <d3d11_1.h>
#include <d3d11_4.h>

#include "avox/AvoxPlayer.h"
#include "avox/AvoxLayer.h"

using namespace avox;

// 24bit BMP 直出 (无外部依赖, 撕裂判读要看图)
static void dumpBmp(const uint8_t* pixels, uint32_t w, uint32_t h,
                    uint32_t pitch, const char* path) {
  const uint32_t rowBytes = w * 3;
  const uint32_t pad = (4 - (rowBytes & 3)) & 3;
  const uint32_t dataSize = (rowBytes + pad) * h;
  const uint32_t fileSize = 54 + dataSize;
  FILE* f = fopen(path, "wb");
  if (!f) return;
  uint8_t hdr[54] = {};
  hdr[0] = 'B'; hdr[1] = 'M';
  memcpy(hdr + 2, &fileSize, 4);
  hdr[10] = 54;                          // pixel data offset
  hdr[14] = 40;                          // BITMAPINFOHEADER
  memcpy(hdr + 18, &w, 4);
  memcpy(hdr + 22, &h, 4);
  hdr[26] = 1;                           // planes
  hdr[28] = 24;                          // bpp
  memcpy(hdr + 34, &dataSize, 4);
  fwrite(hdr, 1, 54, f);
  std::vector<uint8_t> row(rowBytes + pad, 0);
  for (int32_t y = (int32_t)h - 1; y >= 0; --y) {  // BMP 自下而上
    const uint8_t* src = pixels + (uint32_t)y * pitch;
    for (uint32_t x = 0; x < w; ++x) {
      row[x * 3 + 0] = src[x * 4 + 0];  // BGRA -> BGR
      row[x * 3 + 1] = src[x * 4 + 1];
      row[x * 3 + 2] = src[x * 4 + 2];
    }
    fwrite(row.data(), 1, rowBytes + pad, f);
  }
  fclose(f);
  printf("dumped: %s\n", path);
}

// 全行逐字哈希(FNV-1a, 4字节步进): 行 bit 相同则哈希同, 静态区/颗粒差异都分辨
static uint64_t hashRow(const uint8_t* row, uint32_t rowBytes) {
  uint64_t h = 1469598103934665603ull;
  for (uint32_t off = 0; off + 4 <= rowBytes; off += 4) {
    uint32_t v = 0;
    memcpy(&v, row + off, 4);
    h = (h ^ v) * 1099511628211ull;
  }
  return h;
}

int main(int argc, char** argv) {
  const char* video = argc > 1 ? argv[1] : "D://Back//美好.mp4";
  int durationSec = argc > 2 ? atoi(argv[2]) : 10;
  const char* outDir = argc > 3 ? argv[3] : ".";
  int seekMs = argc > 4 ? atoi(argv[4]) : 0;  // 跳到运动段(片头静态测不出撕裂)
  int dumpEverySec = argc > 5 ? atoi(argv[5]) : 0;  // >0: 每隔N秒落地整帧(地面真值取证)
  printf("video: %s, duration: %ds\n", video, durationSec);
  // ── 播放器: VK 离屏管线 + D3D11 共享输出 ──
  IMediaPlayer* mp = createMediaPlayer();
  mp->setHardDecode(true);
  mp->setIoPlan(IoPlan::ffmpeg);
  ISurfaceRender* sr = mp->getSurfaceRender();
  sr->setVulkan(true);
  sr->setSurface(nullptr);
  mp->open(video);
  // 管线异步构建: enable 幂等, 轮询重试直到 outputLayer 就绪
  bool enabled = false;
  for (int i = 0; i < 100 && !enabled; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    enabled = enableVkOutputDx11(sr);
  }
  if (!enabled) {
    printf("FAIL: enableVkOutputDx11 timeout (10s)\n");
    return 1;
  }
  // 字幕联动验证 (与 Unity 时序一致: enable 后加载)
  printf("srt load: %d\n", (int)mp->loadSubtitle("D:/Work/github/avox/assets/video/avox_electron.srt"));
  // ── 轮询共享句柄 (图异步构建) ──
  // 字幕加载等会触发图重建并丢失 dx11 声明, 轮询期幂等重声明 (同 Unity/Godot 桥)
  uint64_t texHandle = 0;
  for (int i = 0; i < 100 && !texHandle; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    enableVkOutputDx11(sr);
    texHandle = getVkOutputDx11Handle(sr);
  }
  if (!texHandle) {
    printf("FAIL: shared texture handle timeout (10s)\n");
    return 1;
  }
  if (seekMs > 0) mp->seek(seekMs);  // 图就绪后再跳, 避开静态片头
  uint64_t fenceHandle = getVkOutputDx11FenceHandle(sr);
  printf("tex NT handle: 0x%llx, fence NT handle: 0x%llx\n",
         (unsigned long long)texHandle, (unsigned long long)fenceHandle);
  // ── 模拟 Unity 设备: 自己建一个 D3D11 设备 ──
  ID3D11Device* devB = nullptr;
  ID3D11DeviceContext* ctxB = nullptr;
  D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_0;
  HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
                                 nullptr, 0, D3D11_SDK_VERSION, &devB, &fl, &ctxB);
  if (FAILED(hr)) {
    printf("FAIL: D3D11CreateDevice deviceB hr=0x%08lx\n", hr);
    return 1;
  }
  // ── 单次 OpenSharedResource1 (D3D11->D3D11 标准共享, 失败不重试) ──
  ID3D11Device1* devB1 = nullptr;
  ID3D11Texture2D* texB = nullptr;
  if (SUCCEEDED(devB->QueryInterface(__uuidof(ID3D11Device1), (void**)&devB1))) {
    hr = devB1->OpenSharedResource1((HANDLE)(uintptr_t)texHandle,
                                    __uuidof(ID3D11Texture2D), (void**)&texB);
    devB1->Release();
  } else {
    hr = E_NOINTERFACE;
  }
  if (FAILED(hr)) {
    printf("FAIL: OpenSharedResource1 hr=0x%08lx\n", hr);
    return 1;
  }
  D3D11_TEXTURE2D_DESC desc = {};
  texB->GetDesc(&desc);
  printf("opened: %ux%u format=%d misc=0x%x\n", desc.Width, desc.Height,
         desc.Format, desc.MiscFlags);
  // staging 只读副本
  D3D11_TEXTURE2D_DESC sdesc = desc;
  sdesc.Usage = D3D11_USAGE_STAGING;
  sdesc.BindFlags = 0;
  sdesc.MiscFlags = 0;
  sdesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  ID3D11Texture2D* staging = nullptr;
  if (FAILED(devB->CreateTexture2D(&sdesc, nullptr, &staging))) {
    printf("FAIL: create staging\n");
    return 1;
  }
  // 打开共享 fence (诊断: 完成值应逐帧单调, 回退=引擎信号问题)
  ID3D11Fence* fenceB = nullptr;
  if (fenceHandle) {
    ID3D11Device5* devB5 = nullptr;
    if (SUCCEEDED(devB->QueryInterface(__uuidof(ID3D11Device5), (void**)&devB5))) {
      devB5->OpenSharedFence((HANDLE)(uintptr_t)fenceHandle,
                             __uuidof(ID3D11Fence), (void**)&fenceB);
      devB5->Release();
    }
    if (!fenceB) printf("warn: OpenSharedFence failed (fence diagnostics disabled)\n");
  }
  // ── 撕裂探针状态 ──
  const uint32_t H = desc.Height;
  const uint32_t rowBytes = desc.Width * 4;
  const size_t bufBytes = (size_t)rowBytes * H;
  // 池>1: 句柄逐帧轮换 —— 按值缓存已开纹理, 新值单次尝试(同 AMD 防重试规矩)
  uint64_t poolHandles[8] = {};
  ID3D11Texture2D* poolTexs[8] = {};
  int poolCacheCount = 0;
  long long poolOpens = 0, poolSwitches = 0;
  // 首开纹理入缓存: 轮换回绕/重建回访直接命中, 不重复 Open; 释放统一走缓存
  poolHandles[0] = texHandle;
  poolTexs[0] = texB;
  poolCacheCount = 1;
  std::vector<uint64_t> refHash(H, 0), curHash(H, 0);       // ref=上一整帧
  std::vector<uint8_t> refPixels(bufBytes, 0);              // prev dump 用像素
  std::vector<uint8_t> candPixels(bufBytes, 0);             // cand dump 用像素
  bool hasRef = false;
  long long nReads = 0, nSame = 0, nFull = 0, nTorn = 0;
  uint64_t fenceRegress = 0, lastFence = 0;
  long long lastDumpMs = -1000000;
  long long lastAssertMs = -1000000;
  auto tStart = std::chrono::steady_clock::now();
  auto tStats = tStart;
  int statsEpoch = 0;
  while (true) {
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now() - tStart)
                       .count();
    if (elapsed > durationSec * 1000) break;
    MSG msg;
    while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&msg);
      DispatchMessage(&msg);
    }
    // 图重建(字幕加载/分辨率协商等)会重造输出层并丢失 dx11 声明 —— 与
    // Unity/Godot 桥同款, 主循环周期性幂等重声明, 不只依赖启动期
    if (elapsed >= lastAssertMs + 500) {
      lastAssertMs = elapsed;
      enableVkOutputDx11(sr);
    }
    // 逐帧轮换/重建句柄跟踪: 池>1 走缓存切换; 池=1 只在句柄变化(图重建)时
    // 单次重开, 防读死纹理. 新句柄开成即重置参考帧(内容流换了, 旧参考必误判)
    {
      const uint64_t curHandle = getVkOutputDx11Handle(sr);
      if (curHandle && curHandle != texHandle) {
        int slot = -1;
        for (int i = 0; i < poolCacheCount; ++i) {
          if (poolHandles[i] == curHandle) {
            slot = i;
            break;
          }
        }
        if (slot >= 0) {
          texB = poolTexs[slot];
          texHandle = curHandle;
          poolSwitches++;
          printf("[ev] t=%lldms switch-cached 0x%llx\n", elapsed,
                 (unsigned long long)curHandle);
        } else {
          ID3D11Device1* d1 = nullptr;
          ID3D11Texture2D* t = nullptr;
          HRESULT ohr = E_NOINTERFACE;
          if (SUCCEEDED(devB->QueryInterface(__uuidof(ID3D11Device1), (void**)&d1))) {
            ohr = d1->OpenSharedResource1((HANDLE)(uintptr_t)curHandle,
                                          __uuidof(ID3D11Texture2D), (void**)&t);
            d1->Release();
          }
          if (SUCCEEDED(ohr) && t && poolCacheCount < 8) {
            poolHandles[poolCacheCount] = curHandle;
            poolTexs[poolCacheCount] = t;
            poolCacheCount++;
            poolOpens++;
            texB = t;
            texHandle = curHandle;
            hasRef = false;  // 新内容流: 参考帧作废, 首读重新采纳
            printf("[ev] t=%lldms open-new 0x%llx ref-reset\n", elapsed,
                   (unsigned long long)curHandle);
          } else if (SUCCEEDED(ohr) && t) {
            // 缓存满: 只切不存(该值不再重开), 引用由 texB 持有到下次切换
            texB = t;
            texHandle = curHandle;
            hasRef = false;
          } else {
            printf("pool handle open failed hr=0x%08lx handle=0x%llx\n", ohr,
                   (unsigned long long)curHandle);
          }
        }
      }
    }
    ctxB->CopyResource(staging, texB);
    D3D11_MAPPED_SUBRESOURCE map = {};
    if (FAILED(ctxB->Map(staging, 0, D3D11_MAP_READ, 0, &map))) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      continue;
    }
    nReads++;
    if (fenceB) {
      const uint64_t fv = fenceB->GetCompletedValue();
      if (fv < lastFence) fenceRegress++;  // 完成值回退=信号取号异常
      lastFence = fv;
    }
    // 行哈希 + 与参考帧比对; 撕裂判据=瞬态性: 变化读后立刻重读同一张纹理,
    // 重读不同=写入进行中被读到(真撕裂), 重读相同=稳定合法帧(慢摇静态重叠
    // 的合法新帧 matchFrac 0.35-0.7, 与撕裂同带, 阈值法不可分 — 09-23 教训)
    uint32_t sameRows = 0;
    for (uint32_t y = 0; y < H; ++y) {
      const uint8_t* row = (const uint8_t*)map.pData + (size_t)y * map.RowPitch;
      curHash[y] = hashRow(row, rowBytes);
      if (hasRef && curHash[y] == refHash[y]) sameRows++;
    }
    if (!hasRef || sameRows == H) {
      nSame++;
      if (!hasRef) {
        for (uint32_t y = 0; y < H; ++y) refHash[y] = curHash[y];
        hasRef = true;
      }
    } else {
      // 内容相对参考有变: 重读判瞬态
      memcpy(candPixels.data(), map.pData, bufBytes);
      ctxB->Unmap(staging, 0);
      ctxB->CopyResource(staging, texB);
      D3D11_MAPPED_SUBRESOURCE remap = {};
      if (FAILED(ctxB->Map(staging, 0, D3D11_MAP_READ, 0, &remap))) {
        continue;
      }
      uint32_t diffRows = 0;
      for (uint32_t y = 0; y < H; ++y) {
        const uint8_t* rrow = (const uint8_t*)remap.pData + (size_t)y * remap.RowPitch;
        if (hashRow(rrow, rowBytes) != curHash[y]) diffRows++;
      }
      ctxB->Unmap(staging, 0);
      if (diffRows * 20 >= H) {
        // 真撕裂: 候选时刻的瞬态内容 vs 写完后的稳定内容
        nTorn++;
        if (nTorn <= 8) {
          char p2[512];
          snprintf(p2, sizeof(p2), "%s/torn%03d_torn.bmp", outDir, (int)nTorn);
          dumpBmp(candPixels.data(), desc.Width, H, rowBytes, p2);
          snprintf(p2, sizeof(p2), "%s/torn%03d_settled.bmp", outDir, (int)nTorn);
          dumpBmp((const uint8_t*)remap.pData, desc.Width, H, remap.RowPitch, p2);
        }
        printf("[ev] t=%lldms TORN diffRows=%u/%u tex=0x%llx\n", elapsed,
               diffRows, H, (unsigned long long)texHandle);
        // 参考不动: 撕裂帧不采纳
      } else {
        // 稳定新帧: 采纳为参考
        nFull++;
        for (uint32_t y = 0; y < H; ++y) refHash[y] = curHash[y];
      }
      continue;  // 已 Unmap/处理完两张
    }
    ctxB->Unmap(staging, 0);
    // 5s 一行统计
    auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration_cast<std::chrono::milliseconds>(now - tStats).count() >= 5000) {
      tStats = now;
      printf("[stats %d] reads=%lld same=%lld full=%lld torn=%lld "
             "fence=%llu regress=%llu\n",
             statsEpoch++, nReads, nSame, nFull, nTorn,
             fenceB ? (unsigned long long)lastFence : 0ull,
             (unsigned long long)fenceRegress);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  printf("---- result: reads=%lld full=%lld torn=%lld "
         "fence-regress=%llu pool-opens=%lld pool-switches=%lld ----\n",
         nReads, nFull, nTorn, (unsigned long long)fenceRegress,
         poolOpens, poolSwitches);
  if (nTorn > 0) {
    printf("TEARS: %lld (transient re-read verified), inspect torn*_*.bmp\n", nTorn);
  }
  bool pass = nReads > 0 && nFull >= 3 && nTorn == 0;
  printf(pass ? "PASS\n" : (nTorn > 0 ? "TEAR-DETECTED\n" : "FAIL\n"));
  if (fenceB) fenceB->Release();
  staging->Release();
  // texB 始终指向缓存内(或未入缓存的泄漏路径不重复释放), 统一释放缓存
  for (int i = 0; i < poolCacheCount; ++i) {
    if (poolTexs[i]) poolTexs[i]->Release();
  }
  ctxB->Release();
  devB->Release();
  disableVkOutputDx11(sr);
  mp->close();
  return pass ? 0 : 2;
}
