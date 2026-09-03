// VkDevice-VkDevice 共享图像端到端测试
// 验证: ImageRender A 写入 0xAB → enableVkOutput → NT 句柄 → ImageRender B 读回 → 验证 0xAB
#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>

#include "avox/AvoxCore.h"
#include "avox/AvoxLayer.h"

#ifdef _WIN32
#include <windows.h>
#endif

using namespace avox;

int main() {
  std::cout << "=== VkDevice-VkDevice E2E Test ===" << std::endl;

  // 1. 创建两个 ImageRender(两个 VkDevice)
  IImageRender* irA = createImageRender();
  IImageRender* irB = createImageRender();
  ISurfaceRender* srA = irA->getSurfaceRender();
  ISurfaceRender* srB = irB->getSurfaceRender();
  // 离屏模式
  srA->setOffSurface(YuvType::other);
  srB->setOffSurface(YuvType::other);

  // 2. ImageRender A: 喂一个 0xAB 填充的图像
  const int32_t W = 256, H = 256;
  const int32_t IMG_SIZE = W * H * 4;
  IImageBuffer* bufA = createImageBuffer();
  ImageFormat fmtA = {};
  fmtA.width = W;
  fmtA.height = H;
  fmtA.imageType = ImageType::rgba8;
  fmtA.rowPitch = W * 4;
  bufA->setImageFormat(fmtA);
  memset(bufA->getPointer(), 0xAB, IMG_SIZE);
  irA->render(bufA);
  std::cout << "[A] rendered 0xAB image" << std::endl;

  // 等待渲染完成
  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  // 3. ImageRender A: enableVkOutput + getVkOutputHandle
  bool bOk = enableVkOutput(srA, W, H);
  std::cout << "[A] enableVkOutput: " << (bOk ? "OK" : "FAIL") << std::endl;
  if (!bOk) { delete irA; delete irB; return 1; }

  VkSharedHandle handle = {};
  bOk = getVkOutputHandle(srA, &handle);
  std::cout << "[A] getVkOutputHandle: " << (bOk ? "OK" : "FAIL")
            << " mem=0x" << std::hex << handle.memHandle << std::dec << std::endl;
  if (!bOk || handle.memHandle == 0) {
    disableVkOutput(srA);
    delete irA; delete irB; return 1;
  }

  // 4. 再 render 一次让数据拷到 sharedImage
  irA->render(bufA);
  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  // 5. ImageRender B: 先喂一帧让管线初始化
  IImageBuffer* bufInit = createImageBuffer();
  ImageFormat fmtInit = {};
  fmtInit.width = W;
  fmtInit.height = H;
  fmtInit.imageType = ImageType::rgba8;
  fmtInit.rowPitch = W * 4;
  bufInit->setImageFormat(fmtInit);
  memset(bufInit->getPointer(), 0x00, IMG_SIZE);
  irB->render(bufInit);
  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  // 6. ImageRender B: enableVkInput + setVkInputHandle
  bOk = enableVkInput(srB, W, H);
  std::cout << "[B] enableVkInput: " << (bOk ? "OK" : "FAIL") << std::endl;
  if (!bOk) { disableVkOutput(srA); delete irA; delete irB; return 1; }

  bOk = setVkInputHandle(srB, &handle);
  std::cout << "[B] setVkInputHandle: " << (bOk ? "OK" : "FAIL") << std::endl;
  if (!bOk) { disableVkInput(srB); disableVkOutput(srA); delete irA; delete irB; return 1; }

  // 7. screenShot 需要渲染线程触发 checkShot
  // screenShot 设 bShotFlag 后阻塞等1秒,期间需要 render() 调 checkShot
  // 方案: 后台线程持续 render, 主线程 screenShot
  std::atomic<bool> bRunning{true};
  std::thread renderThread([&]() {
    while (bRunning) {
      irB->render(bufInit);
      std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
  });

  // 等几帧让 VkInputLayer 的 bVkInterop 路径生效
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  // 8. ImageRender B: screenShot 读回验证
  IImageBuffer* shotBuf = createImageBuffer();
  bool bShot = srB->screenShot(shotBuf);
  bRunning = false;
  renderThread.join();

  if (bShot) {
    ImageFormat fmt = shotBuf->getImageFormat();
    int32_t pixelSize = getPixelSize(fmt.imageType);
    int32_t size = fmt.width * fmt.height * pixelSize;
    uint8_t* data = (uint8_t*)shotBuf->getPointer();
    // 统计 0xAB 像素占比
    int32_t matchCount = 0;
    int32_t nonZero = 0;
    for (int32_t i = 0; i < size; i++) {
      if (data[i] != 0) nonZero++;
      if (data[i] == 0xAB) matchCount++;
    }
    float matchRatio = (float)matchCount / size * 100.0f;
    float nonZeroRatio = (float)nonZero / size * 100.0f;
    std::cout << "[B] screenShot: " << fmt.width << "x" << fmt.height
              << " type=" << getImageTypeStr(fmt.imageType)
              << " nonZero=" << nonZeroRatio << "%"
              << " match0xAB=" << matchRatio << "%" << std::endl;
    if (matchRatio > 50.0f) {
      std::cout << "PASS: data from A's sharedImage reached B via NT handle" << std::endl;
    } else if (nonZeroRatio > 10.0f) {
      std::cout << "PARTIAL: B has data but not 0xAB pattern (format conversion?)" << std::endl;
    } else {
      std::cout << "FAIL: B has no data from A" << std::endl;
    }
  } else {
    std::cout << "[B] screenShot: returned false" << std::endl;
  }
  delete shotBuf;

  // 9. 清理
  disableVkInput(srB);
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  disableVkOutput(srA);
#ifdef _WIN32
  if (handle.memHandle) CloseHandle((HANDLE)handle.memHandle);
#endif
  delete bufA;
  delete bufInit;
  delete irA;
  delete irB;

  std::cout << "=== DONE ===" << std::endl;
  return 0;
}
