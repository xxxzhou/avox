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
  std::vector<uint64_t> refHash(H, 0), curHash(H, 0);       // ref=上一整帧
  std::vector<uint64_t> candHash(H, 0);                     // 候选帧行哈希快照
  std::vector<uint8_t> refPixels(bufBytes, 0);              // prev dump 用像素
  std::vector<uint8_t> candPixels(bufBytes, 0);             // cand dump 用像素
  std::vector<uint8_t> candSameRef(H, 0);                   // 候选行是否同旧帧
  bool hasRef = false;
  bool hasCand = false;
  int candSeq = 0;
  long long nReads = 0, nSame = 0, nFull = 0, nCand = 0, nGarbage = 0;
  uint64_t fenceRegress = 0, lastFence = 0;
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
    // 行哈希 + 与参考帧比对
    uint32_t sameRows = 0;
    for (uint32_t y = 0; y < H; ++y) {
      const uint8_t* row = (const uint8_t*)map.pData + (size_t)y * map.RowPitch;
      curHash[y] = hashRow(row, rowBytes);
      if (hasRef && curHash[y] == refHash[y]) sameRows++;
    }
    const double matchFrac = hasRef ? (double)sameRows / H : 0.0;
    if (!hasRef || matchFrac >= 0.999) {
      // 首帧/同帧重复读: 首帧采纳为参考
      nSame++;
      if (!hasRef) {
        memcpy(refPixels.data(), map.pData, bufBytes);
        for (uint32_t y = 0; y < H; ++y) refHash[y] = curHash[y];
        hasRef = true;
      }
    } else if (matchFrac < 0.35) {
      // 新整帧: 先验证挂起候选(与新帧比对), 再采纳为参考
      nFull++;
      if (hasCand) {
        hasCand = false;
        uint32_t matchRef = 0, matchNew = 0;
        for (uint32_t y = 0; y < H; ++y) {
          if (candSameRef[y]) {
            matchRef++;
          } else {
            const uint8_t* row =
                (const uint8_t*)map.pData + (size_t)y * map.RowPitch;
            if (hashRow(row, rowBytes) == candHash[y]) matchNew++;
          }
        }
        // neither: 候选中与旧帧不同、也与随后新帧不同的行(既非混合也非静态)
        const uint32_t matchNeither = H - matchRef - matchNew;
        printf("[cand%03d verdict] old=%u new=%u neither=%u / %u rows, fence=%llu\n",
               candSeq, matchRef, matchNew, matchNeither, H,
               fenceB ? (unsigned long long)lastFence : 0ull);
        if (matchNeither * 10 >= H) nGarbage++;
        if (candSeq < 12) {  // 三联取证: prev(参考像素)/cand(快照)/next(本帧)
          char p[512];
          snprintf(p, sizeof(p), "%s/tear_cand%03d_prev.bmp", outDir, candSeq);
          dumpBmp(refPixels.data(), desc.Width, H, rowBytes, p);
          snprintf(p, sizeof(p), "%s/tear_cand%03d_cand.bmp", outDir, candSeq);
          dumpBmp(candPixels.data(), desc.Width, H, rowBytes, p);
          snprintf(p, sizeof(p), "%s/tear_cand%03d_next.bmp", outDir, candSeq);
          dumpBmp((const uint8_t*)map.pData, desc.Width, H, map.RowPitch, p);
        }
        candSeq++;
      }
      memcpy(refPixels.data(), map.pData, bufBytes);
      for (uint32_t y = 0; y < H; ++y) refHash[y] = curHash[y];
    } else {
      // 部分行同旧帧: 撕裂候选(或高静态重叠的合法新帧, 看 dump 判读)
      nCand++;
      memcpy(candPixels.data(), map.pData, bufBytes);
      for (uint32_t y = 0; y < H; ++y) {
        candSameRef[y] = (curHash[y] == refHash[y]) ? 1 : 0;
        candHash[y] = curHash[y];
      }
      hasCand = true;
    }
    ctxB->Unmap(staging, 0);
    // 5s 一行统计
    auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration_cast<std::chrono::milliseconds>(now - tStats).count() >= 5000) {
      tStats = now;
      printf("[stats %d] reads=%lld same=%lld full=%lld cand=%lld garbage=%lld "
             "fence=%llu regress=%llu\n",
             statsEpoch++, nReads, nSame, nFull, nCand, nGarbage,
             fenceB ? (unsigned long long)lastFence : 0ull,
             (unsigned long long)fenceRegress);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  printf("---- result: reads=%lld full=%lld cand=%lld garbage=%lld "
         "fence-regress=%llu ----\n",
         nReads, nFull, nCand, nGarbage, (unsigned long long)fenceRegress);
  if (nCand > 0) {
    printf("TEAR CANDIDATES: %d dumped, inspect tear_cand*_{prev,cand,next}.bmp "
           "(torn = cand is a time-mixed horizontal cut of prev/next)\n",
           candSeq < 12 ? candSeq : 12);
  }
  bool pass = nReads > 0 && nFull >= 3 && nCand == 0 && nGarbage == 0;
  printf(pass ? "PASS\n" : (nCand > 0 || nGarbage > 0 ? "TEAR-SUSPECT\n" : "FAIL\n"));
  if (fenceB) fenceB->Release();
  staging->Release();
  texB->Release();
  ctxB->Release();
  devB->Release();
  disableVkOutputDx11(sr);
  mp->close();
  return pass ? 0 : 2;
}
