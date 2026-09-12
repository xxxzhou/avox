// GPU 直通到前台窗口: 硬解(DX11) -> Vulkan 合成(字幕/处理) -> 双路验证
//   路A: VkWindow 交换链直接上屏 (Vulkan 与前台窗口直通)
//   路B: enableVkOutputDx11 NT 共享纹理 -> 独立 D3D11 设备读回 (Unity 同款消费)
// 判定: 共享纹理校验和持续变化 + fence 前进 + 转储一帧地面真值
// 跑法: dx11windowtest [视频路径] [秒数], 默认标准测试源 h264 640x360
#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>

#include <d3d11.h>
#include <d3d11_1.h>
#include <d3d11_4.h>
#include <windows.h>

#include "avox/AvoxPlayer.h"
#include "avox/AvoxLayer.h"

using namespace avox;

static HWND g_hwnd = nullptr;
static std::atomic<bool> g_windowClosed{false};

static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_DESTROY:
      g_windowClosed = true;
      PostQuitMessage(0);
      return 0;
    case WM_KEYDOWN:
      if (wp == VK_ESCAPE) {
        DestroyWindow(hwnd);
      }
      return 0;
  }
  return DefWindowProcA(hwnd, msg, wp, lp);
}

static bool createWindow(int32_t width, int32_t height) {
  WNDCLASSA wc = {};
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = wndProc;
  wc.hInstance = GetModuleHandleA(nullptr);
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.lpszClassName = "avox_dx11windowtest";
  if (!RegisterClassA(&wc)) {
    printf("FAIL: RegisterClassA\n");
    return false;
  }
  RECT rc = {0, 0, width, height};
  AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
  g_hwnd = CreateWindowA(wc.lpszClassName, "avox gpu passthrough (dx11 hard decode -> vulkan)",
                         WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT,
                         rc.right - rc.left, rc.bottom - rc.top, nullptr, nullptr,
                         wc.hInstance, nullptr);
  if (!g_hwnd) {
    printf("FAIL: CreateWindowA\n");
    return false;
  }
  ShowWindow(g_hwnd, SW_SHOW);
  UpdateWindow(g_hwnd);
  return true;
}

static uint64_t sampleChecksum(ID3D11DeviceContext* ctx, ID3D11Texture2D* staging,
                               const D3D11_TEXTURE2D_DESC& desc) {
  D3D11_MAPPED_SUBRESOURCE map = {};
  if (FAILED(ctx->Map(staging, 0, D3D11_MAP_READ, 0, &map))) {
    return 0;
  }
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
  const char* video = argc > 1 ? argv[1]
                               : "D:/Work/github/avox/assets/video/test/test_h264_aac_640x360.mp4";
  int durationSec = argc > 2 ? atoi(argv[2]) : 10;
  printf("video: %s, duration: %ds\n", video, durationSec);
  if (!createWindow(960, 600)) {
    return 1;
  }

  // ── 播放器: DX11 硬解 + Vulkan 窗口直渲 (前台交换链) ──
  IMediaPlayer* mp = createMediaPlayer();
  mp->setHardDecode(true);
  mp->setIoPlan(IoPlan::ffmpeg);
  ISurfaceRender* sr = mp->getSurfaceRender();
  sr->setVulkan(true);
  sr->setSurface(g_hwnd);
  mp->open(video);
  // 字幕联动 (Vulkan 合成层, 与 Unity 时序一致: enable 后加载)
  if (mp->getSubtitle()) {
    printf("srt load: %d\n",
           (int)mp->getSubtitle()->loadSrt("D:/Work/github/avox/assets/video/avox_electron.srt"));
  }

  // ── 路B: D3D11 NT 共享纹理导出 (图异步构建, 幂等重声明直到句柄就绪) ──
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

  // ── 模拟 Unity 消费设备: 独立 D3D11 设备打开 (有界重试: 图重建期句柄会
  // churn, 失败后等新句柄再试; 同句柄绝不立即重试 —— AMD 驱动安全规矩) ──
  ID3D11Device* devB = nullptr;
  ID3D11DeviceContext* ctxB = nullptr;
  D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_0;
  if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
                               D3D11_SDK_VERSION, &devB, &fl, &ctxB))) {
    printf("FAIL: D3D11CreateDevice deviceB\n");
    return 1;
  }
  ID3D11Device1* devB1 = nullptr;
  ID3D11Texture2D* texB = nullptr;
  HRESULT hr = E_NOINTERFACE;
  for (int attempt = 0; attempt < 20 && !texB; ++attempt) {
    if (SUCCEEDED(devB->QueryInterface(__uuidof(ID3D11Device1), (void**)&devB1))) {
      hr = devB1->OpenSharedResource1((HANDLE)(uintptr_t)texHandle,
                                      __uuidof(ID3D11Texture2D), (void**)&texB);
      devB1->Release();
    }
    if (SUCCEEDED(hr)) break;
    texB = nullptr;
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    enableVkOutputDx11(sr);
    uint64_t fresh = getVkOutputDx11Handle(sr);
    if (fresh) texHandle = fresh;
  }
  if (FAILED(hr) || !texB) {
    printf("FAIL: OpenSharedResource1 hr=0x%08lx (20 attempts)\n", (unsigned long)hr);
    return 1;
  }
  D3D11_TEXTURE2D_DESC desc = {};
  texB->GetDesc(&desc);
  printf("opened: %ux%u format=%d misc=0x%x\n", desc.Width, desc.Height, desc.Format,
         desc.MiscFlags);
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
  ID3D11Fence* fenceB = nullptr;
  if (fenceHandle) {
    ID3D11Device5* devB5 = nullptr;
    if (SUCCEEDED(devB->QueryInterface(__uuidof(ID3D11Device5), (void**)&devB5))) {
      devB5->OpenSharedFence((HANDLE)(uintptr_t)fenceHandle, __uuidof(ID3D11Fence),
                             (void**)&fenceB);
      devB5->Release();
    }
  }

  // ── 主循环: 前台窗口渲染 (路A) + 共享纹理读回验证 (路B) ──
  // 图重建(字幕/字体层加载等)会丢失 dx11 声明并换句柄 —— 同 Unity/Godot 桥,
  // 周期性幂等重声明 + 句柄变化时单次重开 (OpenSharedResource1 仍失败不重试)
  uint64_t lastSum = 0;
  int changedFrames = 0, totalReads = 0, dumpCount = 0, reopenCount = 0;
  auto tStart = std::chrono::steady_clock::now();
  auto lastReassert = tStart;
  while (!g_windowClosed) {
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now() - tStart)
                       .count();
    if (elapsed > durationSec * 1000) break;
    MSG msg;
    while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
      if (msg.message == WM_QUIT) g_windowClosed = true;
      TranslateMessage(&msg);
      DispatchMessage(&msg);
    }
    // 幂等重声明 (500ms 一次): dx11Output 标志跨图重建会还原为 0
    auto sinceReassert = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - lastReassert)
                             .count();
    if (sinceReassert > 500) {
      lastReassert = std::chrono::steady_clock::now();
      enableVkOutputDx11(sr);
      uint64_t fresh = getVkOutputDx11Handle(sr);
      if (fresh && fresh != texHandle) {
        printf("[%3lldms] handle changed 0x%llx -> 0x%llx, reopen\n",
               (long long)elapsed, (unsigned long long)texHandle,
               (unsigned long long)fresh);
        texHandle = fresh;
        if (staging) staging->Release();
        if (texB) texB->Release();
        texB = nullptr;
        staging = nullptr;
        hr = E_NOINTERFACE;
        if (SUCCEEDED(devB->QueryInterface(__uuidof(ID3D11Device1), (void**)&devB1))) {
          hr = devB1->OpenSharedResource1((HANDLE)(uintptr_t)texHandle,
                                          __uuidof(ID3D11Texture2D), (void**)&texB);
          devB1->Release();
        }
        if (SUCCEEDED(hr)) {
          texB->GetDesc(&desc);
          sdesc = desc;
          sdesc.Usage = D3D11_USAGE_STAGING;
          sdesc.BindFlags = 0;
          sdesc.MiscFlags = 0;
          sdesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
          if (FAILED(devB->CreateTexture2D(&sdesc, nullptr, &staging))) {
            printf("FAIL: recreate staging\n");
            break;
          }
          reopenCount++;
          lastSum = 0;
        }
      }
    }
    if (!texB) {
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
      continue;
    }
    ctxB->CopyResource(staging, texB);
    uint64_t sum = sampleChecksum(ctxB, staging, desc);
    if (elapsed > 5000 && dumpCount == 0) {
      dumpCount++;
      dumpPpm(ctxB, staging, desc, "window_dump.ppm");
    }
    if (sum && sum != lastSum) {
      changedFrames++;
      UINT64 fv = fenceB ? fenceB->GetCompletedValue() : 0;
      printf("[%3lldms] checksum changed (#%d), fence=%llu\n", (long long)elapsed,
             changedFrames, (unsigned long long)fv);
      lastSum = sum;
    } else if (sum) {
      totalReads++;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
  }

  bool pass = totalReads > 0 && changedFrames >= 3;
  printf("---- result: reads=%d changed=%d fence=%s window=%s reopen=%d ----\n",
         totalReads, changedFrames, fenceB ? "ok" : "unavailable",
         g_hwnd ? "alive" : "closed", reopenCount);
  printf(pass ? "PASS\n" : "FAIL\n");
  if (fenceB) fenceB->Release();
  if (staging) staging->Release();
  if (texB) texB->Release();
  if (ctxB) ctxB->Release();
  if (devB) devB->Release();
  disableVkOutputDx11(sr);
  mp->close();
  if (g_hwnd) {
    DestroyWindow(g_hwnd);
  }
  return pass ? 0 : 1;
}
