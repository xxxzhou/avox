// D3D11 共享输出通路验证 (模拟 Unity D3D11 后端):
// avox VK 管线 -> 底层自建 NT 共享纹理 -> 本进程第二个 D3D11 设备打开并复制读回
// 安全规矩: OpenSharedResource1 仅单次尝试, 失败即退出, 严禁重试
#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>

#include <d3d11.h>
#include <d3d11_1.h>
#include <d3d11_4.h>

#include "avox/AvoxPlayer.h"
#include "avox/AvoxLayer.h"

using namespace avox;

static uint64_t sampleChecksum(ID3D11DeviceContext* ctx, ID3D11Texture2D* staging,
                               const D3D11_TEXTURE2D_DESC& desc) {
  D3D11_MAPPED_SUBRESOURCE map = {};
  if (FAILED(ctx->Map(staging, 0, D3D11_MAP_READ, 0, &map))) {
    return 0;
  }
  // 网格采样: 首像素 + 中心 + 若干网格点, 记录 BGRA 原始字节
  uint64_t sum = 0;
  int points[][2] = {{2, 2}, {desc.Width / 2, desc.Height / 2},
                     {desc.Width - 3, desc.Height - 3}, {desc.Width / 4, desc.Height / 2},
                     {(desc.Width * 3) / 4, desc.Height / 2}};
  for (auto& pt : points) {
    const uint8_t* p = (const uint8_t*)map.pData + pt[1] * map.RowPitch + pt[0] * 4;
    sum = sum * 1000003u + p[0] + p[1] * 256u + p[2] * 65536u + p[3] * 16777216u;
  }
  ctx->Unmap(staging, 0);
  return sum;
}

// 共享纹理内容转储 (D3D11 侧视角的地面真值, 行序按映射顺序 = 纹理行序)
static void dumpPpm(ID3D11DeviceContext* ctx, ID3D11Texture2D* staging,
                    const D3D11_TEXTURE2D_DESC& desc, const char* path) {
  D3D11_MAPPED_SUBRESOURCE map = {};
  if (FAILED(ctx->Map(staging, 0, D3D11_MAP_READ, 0, &map))) {
    return;
  }
  FILE* f = fopen(path, "wb");
  if (f) {
    fprintf(f, "P6\n%u %u\n255\n", desc.Width, desc.Height);
    for (UINT y = 0; y < desc.Height; ++y) {
      const uint8_t* row = (const uint8_t*)map.pData + y * map.RowPitch;
      for (UINT x = 0; x < desc.Width; ++x) {
        fwrite(row + x * 4, 1, 3, f);
      }
    }
    fclose(f);
    printf("dumped: %s\n", path);
  }
  ctx->Unmap(staging, 0);
}

int main(int argc, char** argv) {
  const char* video = argc > 1 ? argv[1] : "D://Back//美好.mp4";
  int durationSec = argc > 2 ? atoi(argv[2]) : 10;
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
  if (mp->getSubtitle()) {
    printf("srt load: %d\n", (int)mp->getSubtitle()->loadSrt("D:/Work/github/avox/assets/video/avox_electron.srt"));
  }
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
  // 打开共享 fence (可选)
  ID3D11Fence* fenceB = nullptr;
  if (fenceHandle) {
    ID3D11Device5* devB5 = nullptr;
    if (SUCCEEDED(devB->QueryInterface(__uuidof(ID3D11Device5), (void**)&devB5))) {
      devB5->OpenSharedFence((HANDLE)(uintptr_t)fenceHandle,
                             __uuidof(ID3D11Fence), (void**)&fenceB);
      devB5->Release();
    }
    if (!fenceB) printf("warn: OpenSharedFence failed (sync polling disabled)\n");
  }
  // ── 读回循环: 每帧复制->读回->校验, 检测画面变化 ──
  uint64_t lastSum = 0;
  int changedFrames = 0, totalReads = 0, dumpCount = 0;
  auto tStart = std::chrono::steady_clock::now();
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
    uint64_t sum = sampleChecksum(ctxB, staging, desc);
    if (elapsed > 5000 && dumpCount == 0) {
      dumpCount++;
      dumpPpm(ctxB, staging, desc, "shared_dump.ppm");
    }
    if (sum) {
      totalReads++;
      if (sum != lastSum) {
        changedFrames++;
        UINT64 fv = fenceB ? fenceB->GetCompletedValue() : 0;
        printf("[%3lldms] checksum changed (#%d), fence=%llu\n",
               (long long)elapsed, changedFrames, (unsigned long long)fv);
        lastSum = sum;
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
  }
  printf("---- result: reads=%d changed=%d fence=%s ----\n", totalReads,
         changedFrames, fenceB ? "ok" : "unavailable");
  bool pass = totalReads > 0 && changedFrames >= 3;
  printf(pass ? "PASS\n" : "FAIL\n");
  if (fenceB) fenceB->Release();
  staging->Release();
  texB->Release();
  ctxB->Release();
  devB->Release();
  disableVkOutputDx11(sr);
  mp->close();
  return pass ? 0 : 1;
}
