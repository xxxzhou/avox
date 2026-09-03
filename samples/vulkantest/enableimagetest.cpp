// Phase 1 验证 sample: ISurfaceRender::enableImage 每帧 GPU 缩放 + 零拷贝写入用户 IImageBuffer
// 验证点:
//   1. 缩图: buf 内容是按 imageOutFormat (192x192) 缩放后的 RGBA
//   2. 零拷贝: imgBuf->bDataRef()==true (getPointer 直接指向 VkOutputLayer staging 映射内存)
//   3. 同帧安全: onRender 里读取即当前帧 (VkPipeGraph::onRun 已 vkWaitForFences)
//
// 运行: enableimagetest [视频路径]  (默认 D://Back//美好_h265.mp4)
// 交互: S 存当前帧 PNG, ESC 退出
#include <atomic>
#include <chrono>
#include <thread>

#include "avox/Avox.hpp"
#include "avox/AvoxLayer.h"
#include "avox/AvoxPlayer.h"
#include "avox/AvoxVideo.h"
#include "avox/module/LogHelper.hpp"
#include "avox_vulkan/VkTemplate.hpp"

#ifdef _WIN32
#include <windows.h>

#include "avox_windows/WinCommon.hpp"
#endif

using namespace avox;

// 缩放目标尺寸 (MediaPipe face_landmarker 输入 192x192)
static const int32_t kOutW = 192;
static const int32_t kOutH = 192;
// 默认视频 (可用 argv[1] 覆盖)
static const char* kUrl = "D://Back//美好_h265.mp4";

IMediaPlayer* mp = nullptr;
// 调用方持有的输出 buffer (生命周期维持到 disableImage 之后)
IImageBuffer* imgBuf = nullptr;

// 订阅 ISurfaceRender, 每帧读取 imgBuf 验证零拷贝/缩图/同帧安全
class ImageVerifyOb : public ISurfaceRenderOb {
 public:
  std::atomic<int> frameCount{0};
  void onRender() override {
    int n = ++frameCount;
    // 前 10 帧详细打印, 之后每 120 帧心跳一次
    bool bDetail = (n <= 10) || (n % 120 == 0);
    if (!bDetail) {
      return;
    }
    ImageFormat fmt = imgBuf->getImageFormat();
    uint8_t* p = imgBuf->getPointer();
    log(LogLevel::info, "[enableImage] frame:", n, " ", fmt.width, "x",
        fmt.height, " rowPitch:", fmt.rowPitch, " bufSize:",
        imgBuf->getBufferSize(), " bDataRef:", imgBuf->bDataRef(),
        " type:", getImageTypeStr(fmt.imageType));
    if (p && fmt.rowPitch > 0) {
      // 采样左上角与中心像素, 判断非全黑
      int32_t mid = fmt.rowPitch * (fmt.height / 2) + (fmt.width / 2) * 4;
      log(LogLevel::info, "  px(0,0)=", (int)p[0], (int)p[1], (int)p[2],
          (int)p[3], " px(mid)=", (int)p[mid], (int)p[mid + 1],
          (int)p[mid + 2], (int)p[mid + 3]);
    }
    // 第 5 帧存盘目视确认缩图内容
    if (n == 5) {
      saveImagePath("D://enableimage_frame.png", imgBuf);
      log(LogLevel::info, "  saved D://enableimage_frame.png");
    }
  }
};

ImageVerifyOb verifyOb;

int main(int argc, char* argv[]) {
  const char* url = (argc > 1) ? argv[1] : kUrl;
  mp = createMediaPlayer();
  // 调用方负责设定 ImageFormat (宽/高/类型)
  imgBuf = createImageBuffer();
  ImageFormat fmt = {};
  fmt.width = kOutW;
  fmt.height = kOutH;
  fmt.imageType = ImageType::rgba8;
  imgBuf->setImageFormat(fmt);
  // 离屏渲染 (无可见窗口), 帧仍走 Vk 管线
  mp->getSurfaceRender()->setSurface(nullptr);
  // 先订阅再 enableImage, 保证首帧即能读到
  addSurfaceRenderOb(mp->getSurfaceRender(), &verifyOb);
  mp->getSurfaceRender()->enableImage(imgBuf);
  mp->setIoPlan(IoPlan::ffmpeg);
  mp->open(url);
  log(LogLevel::info, "enableimagetest running, url:", url);
  // win32 消息循环 (Vk 隐藏窗口内部仍需 message pump)
  bool m_running = true;
  MSG msg;
  while (m_running) {
    while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
      if (msg.message == WM_QUIT) {
        m_running = false;
        break;
      }
      if (msg.message == WM_KEYDOWN) {
        int vk = (int)msg.wParam;
        switch (vk) {
          case 'S': {
            saveImagePath("D://enableimage_frame.png", imgBuf);
            log(LogLevel::info, "manual save D://enableimage_frame.png");
            break;
          }
          case VK_ESCAPE: {
            m_running = false;
            break;
          }
          default:
            break;
        }
      }
      TranslateMessage(&msg);
      DispatchMessage(&msg);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  mp->getSurfaceRender()->disableImage();
  removeSurfaceRenderOb(mp->getSurfaceRender(), &verifyOb);
  mp->close();
  delete imgBuf;
  return 0;
}
