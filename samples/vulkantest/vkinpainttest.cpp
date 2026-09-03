#include <thread>

#include "avox/AvoxPlayer.h"
#include "avox_vulkan/VkTemplate.hpp"
#include "avox_windows/WinExport.h"


#ifdef AVOX_ENABLE_FREETYPE
#include "avox_freetype/FreetypeExport.h"
#endif

#ifdef _WIN32
#include <windows.h>

#include "avox_windows/WinCommon.hpp"
#endif

#include "avox/Avox.hpp"
#include "avox/AvoxVideo.h"


using namespace avox;

IMediaPlayer* mp = nullptr;
ISurfaceRender* render = nullptr;
bool bWatermark = false;
bool bInpaint = false;
IImageBuffer* watermarkImage = nullptr;

int main() {
  mp = createMediaPlayer();
  mp->setHardDecode(true);
  mp->getSurfaceRender()->setSurface(nullptr);
  mp->getOption()->setBool("mp.lowlatency", true);
  mp->getOption()->setNumber("mp.lowlatency.speed", 1.2);
  mp->getOption()->setInt("mp.delay.ms", 500);
  mp->getOption()->setString("io.rtsp.transport", "tcp");
  mp->getOption()->setInt("io.timeout.ms", 4000);
  mp->setIoPlan(IoPlan::ffmpeg);

  // 设置水印 - 使用 blend.bmp 图片
  Watermark wm = {};
  wm.centerX = 0.7f;
  wm.centerY = 0.3f;
  wm.width = 0.2f;
  wm.height = 0.2f;
  wm.alaph = 0.4f;

  // 加载水印图片
  watermarkImage = createImageBuffer();
  if (!loadImagePath("blend.bmp", watermarkImage)) {
    printf("[InpaintTest] Warning: blend.bmp not found, watermark will be empty\n");
  }

  render = mp->getSurfaceRender();
  render->enableWatermark(wm, watermarkImage);
  // 打开测试视频 - 使用本地视频文件
  // 如果没有视频文件，会使用默认的测试图案
  mp->open("D://Back//test.mp4");

          
  bWatermark = true;
  bInpaint = true;
  // 打印操作提示
  printf("[InpaintTest] Controls:\n");
  printf("  1: Enable Watermark\n");
  printf("  2: Disable Watermark\n");
  printf("  3: Enable Inpaint (remove watermark)\n");
  printf("  4: Disable Inpaint\n");
  printf("  5: Enable Watermark + Inpaint\n");
  printf("  6: Disable Watermark (Inpaint still on)\n");
  printf("  ESC: Exit\n");

  // win32 消息循环
  bool m_running = true;
  MSG msg;
  while (m_running) {
    // 处理所有待处理的消息
    while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
      if (msg.message == WM_QUIT) {
        m_running = false;
        break;
      }
      if (msg.message == WM_KEYDOWN) {
        // 获取虚拟键码
        int virtualKey = (int)msg.wParam;
        switch (virtualKey) {
        case '1':  // 开水印
          if (!bWatermark && render) {
            render->enableWatermark(wm, watermarkImage);
            bWatermark = true;
            printf("[InpaintTest] Watermark enabled\n");
          }
          break;
        case '2':  // 关水印
          if (bWatermark && render) {
            render->disableWatermark();
            bWatermark = false;
            printf("[InpaintTest] Watermark disabled\n");
          }
          break;
        case '3':  // 开去水印
          if (!bInpaint && render) {           
            bInpaint = true;
            printf("[InpaintTest] Inpaint enabled\n");
          }
          break;
        case '4':  // 关去水印
          if (bInpaint && render) {            
            bInpaint = false;
            printf("[InpaintTest] Inpaint disabled\n");
          }
          break;
        case '5':  // 开水印+去水印
          if (render) {
            render->enableWatermark(wm, watermarkImage);            
            bWatermark = true;
            bInpaint = true;
            printf("[InpaintTest] Watermark + Inpaint enabled\n");
          }
          break;
        case '6':  // 关水印+去水印
          if (render) {
            render->disableWatermark();
            // inpaint 保持开启，可以看到去水印效果
            // render->disableInpaint();
            bWatermark = false;
            // bInpaint = false;
            printf("[InpaintTest] Watermark disabled, Inpaint still on\n");
          }
          break;
        case VK_ESCAPE:
          m_running = false;
          break;
        default:
          break;
        }
      }
      TranslateMessage(&msg);
      DispatchMessage(&msg);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }

  if (render) {
    render->disableWatermark();
  }
  if (watermarkImage) {
    delete watermarkImage;
    watermarkImage = nullptr;
  }

  mp->close();
  return 0;
}