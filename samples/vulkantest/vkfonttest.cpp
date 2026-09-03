// =============================================================================
// vkfonttest - 多位置文本渲染测试 (IFontLayer 新接口)
// =============================================================================
// 按键说明:
// ┌──────────────────┬────────┬─────────────────────────────────────────┐
// │       功能       │  按键  │                  说明                    │
// ├──────────────────┼────────┼─────────────────────────────────────────┤
// │ 3 个文本位置     │ 启动时 │ index 0=左上, 1=中央, 2=右下角时间戳    │
// │ 切换当前编辑位置 │ 1/2/3  │ setTextLayout(index)                    │
// │ 读取排版         │ R      │ getLayout(index) 打印到 log              │
// │ 自动 resize      │ 启动时 │ getLayout(5) → 触发 resize(6)           │
// │ 全局颜色         │ C      │ setColor() 循环 5 种颜色                 │
// │ 全局缩放         │ S      │ setScale() 切换 1.0/1.5/2.0/3.0         │
// │ 切换字体         │ F      │ setFont() simhei.ttf 16/24              │
// │ 多位置同时渲染   │ 每帧   │ 3 个 block 共享一个 canvas，一次 dispatch│
// │ 自定义文本       │ T      │ 在当前选中位置绘制 "Block N - Test"     │
// │ 禁用/重新启用    │ X / Z  │ disableRenderFont / enableRenderFont     │
// │ 时间戳自动刷新   │ 每秒   │ Block 2 右下角 HH:MM:SS 每秒更新       │
// └──────────────────┴────────┴─────────────────────────────────────────┘

#include <chrono>
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



using namespace avox;

IMediaPlayer* mp = nullptr;
ISurfaceRender* wr = nullptr;
#ifdef AVOX_ENABLE_FREETYPE
IFontLayer* fontLayer = nullptr;
#endif

// 多位置文本测试索引
int32_t gActiveIndex = 0;

#ifdef AVOX_ENABLE_FREETYPE
// 初始化三个位置的字体排版
void initFontLayouts() {
  if (!fontLayer) return;

  // index 0: 左上角 - 状态提示
  FontLayout layout0 = {};
  layout0.alignment.horizontal = HAlignType::left;
  layout0.alignment.vertical = VAlignType::top;
  layout0.x = 0.02f;
  layout0.y = 0.02f;
  layout0.width = 0.5f;
  layout0.height = 0.2f;
  fontLayer->updateLayout(0, layout0);

  // index 1: 中央 - 主标题
  FontLayout layout1 = {};
  layout1.alignment.horizontal = HAlignType::mid;
  layout1.alignment.vertical = VAlignType::mid;
  layout1.x = 0.5f;
  layout1.y = 0.4f;
  layout1.width = 0.8f;
  layout1.height = 0.3f;
  fontLayer->updateLayout(1, layout1);

  // index 2: 右下角 - 时间戳
  FontLayout layout2 = {};
  layout2.alignment.horizontal = HAlignType::right;
  layout2.alignment.vertical = VAlignType::bottom;
  layout2.x = 0.98f;
  layout2.y = 0.95f;
  layout2.width = 0.4f;
  layout2.height = 0.1f;
  fontLayer->updateLayout(2, layout2);

  log(LogLevel::info, "Font layouts initialized: 3 text blocks");
}

// 更新 block 0 和 1 的文本（block 2 时间戳由主循环每秒更新）
void updateAllTexts() {
  if (!fontLayer) return;

  // index 0: 按键提示
  fontLayer->setTextLayout(0);
  fontLayer->drawText(
      "[1/2/3]Switch Block  [C]Change Color  [S]Scale  [F]SetFont  "
      "[R]Read Layout");

  // index 1: 当前选中的 block 编号
  char title[64];
  snprintf(title, sizeof(title), "Active Block: %d", gActiveIndex);
  fontLayer->setTextLayout(1);
  fontLayer->drawText(title);
}

// 切换颜色
void cycleColor() {
  if (!fontLayer) return;
  static int colorIdx = 0;
  const float colors[][4] = {
      {1.0f, 1.0f, 1.0f, 0.0f},   // 白色不透明
      {1.0f, 0.0f, 0.0f, 0.3f},   // 红色半透明
      {0.0f, 1.0f, 0.0f, 0.0f},   // 绿色不透明
      {0.0f, 0.0f, 1.0f, 0.5f},   // 蓝色半透明
      {1.0f, 1.0f, 0.0f, 0.0f},   // 黄色不透明
  };
  colorIdx = (colorIdx + 1) % 5;
  fontLayer->setColor(colors[colorIdx][0], colors[colorIdx][1],
                      colors[colorIdx][2], colors[colorIdx][3]);
  log(LogLevel::info, "Color cycled to index:", colorIdx);
}
#endif

int main() {
  mp = createMediaPlayer();
  wr = mp->getSurfaceRender();
  wr->setSurface(nullptr);
  mp->setHardDecode(false);
  mp->getOption()->setNumber("mp.lowlatency.speed", 1.2);
  mp->getOption()->setInt("mp.delay.ms", 2500);
  mp->getOption()->setInt("io.timeout.ms", 10000);
  mp->setIoPlan(IoPlan::ffmpeg);
  mp->open("D://Back//美好.mp4");

#ifdef AVOX_ENABLE_FREETYPE
  fontLayer = enableRenderFont(mp->getSurfaceRender());
  fontLayer->setFont("simhei.ttf", 16);
  fontLayer->setColor(1.0f, 1.0f, 1.0f, 0.0f);
  fontLayer->setScale(2.0f);

  // 初始化三个位置的排版
  initFontLayouts();

  // 验证 getLayout: 读取 index 0 的排版
  FontLayout readBack = fontLayer->getLayout(0);
  log(LogLevel::info, "getLayout(0): x=", readBack.x, " y=", readBack.y,
      " width=", readBack.width);

  // getLayout 自动 resize: 读取 index 5（触发 resize(6)）
  FontLayout autoResized = fontLayer->getLayout(5);
  log(LogLevel::info, "getLayout(5) auto-resize: x=", autoResized.x,
      " (default)");

  updateAllTexts();
#endif

  bool m_running = true;
  MSG msg;
  auto lastTimeUpdate = std::chrono::steady_clock::now();
  while (m_running) {
    while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
      if (msg.message == WM_QUIT) {
        m_running = false;
        break;
      }
      if (msg.message == WM_KEYDOWN) {
        int virtualKey = (int)msg.wParam;
        log(LogLevel::info, "key pressed:", (char)virtualKey);
#ifdef AVOX_ENABLE_FREETYPE
        switch (virtualKey) {
          // 1/2/3: 切换到对应 index
          case '1':
            gActiveIndex = 0;
            fontLayer->setTextLayout(0);
            log(LogLevel::info, "Switch to text block 0 (top-left)");
            break;
          case '2':
            gActiveIndex = 1;
            fontLayer->setTextLayout(1);
            log(LogLevel::info, "Switch to text block 1 (center)");
            break;
          case '3':
            gActiveIndex = 2;
            fontLayer->setTextLayout(2);
            log(LogLevel::info, "Switch to text block 2 (bottom-right)");
            break;
          // C: 循环颜色
          case 'C':
            cycleColor();
            break;
          // S: 切换缩放
          case 'S': {
            static float scales[] = {1.0f, 1.5f, 2.0f, 3.0f};
            static int scaleIdx = 2;
            scaleIdx = (scaleIdx + 1) % 4;
            fontLayer->setScale(scales[scaleIdx]);
            log(LogLevel::info, "Scale set to:", scales[scaleIdx]);
            break;
          }
          // F: 切换字体
          case 'F': {
            static bool toggle = false;
            toggle = !toggle;
            if (toggle) {
              fontLayer->setFont("simhei.ttf", 24);
              log(LogLevel::info, "Font: simhei.ttf 24");
            } else {
              fontLayer->setFont("simhei.ttf", 16);
              log(LogLevel::info, "Font: simhei.ttf 16");
            }
            break;
          }
          // R: 读取当前 index 的 layout
          case 'R': {
            FontLayout curLayout = fontLayer->getLayout(gActiveIndex);
            log(LogLevel::info, "Layout[", gActiveIndex, "]: x=", curLayout.x,
                " y=", curLayout.y, " w=", curLayout.width,
                " h=", curLayout.height);
            break;
          }
          // T: 在当前激活位置绘制自定义文本
          case 'T': {
            char testText[64];
            snprintf(testText, sizeof(testText),
                     "Block %d - Custom Text Test", gActiveIndex);
            fontLayer->drawText(testText);
            log(LogLevel::info, "Custom text drawn at block", gActiveIndex);
            break;
          }
          // X: 禁用/启用字体
          case 'X':
            disableRenderFont(mp->getSurfaceRender());
            fontLayer = nullptr;
            log(LogLevel::info, "Font disabled");
            break;
          case 'Z':
            fontLayer = enableRenderFont(mp->getSurfaceRender());
            fontLayer->setFont("simhei.ttf", 16);
            fontLayer->setColor(1.0f, 1.0f, 1.0f, 0.0f);
            fontLayer->setScale(2.0f);
            initFontLayouts();
            log(LogLevel::info, "Font re-enabled");
            break;
          default:
            break;
        }
        // 按键后更新 block 0 和 1（tips + active block）
        updateAllTexts();
#endif
      }
      TranslateMessage(&msg);
      DispatchMessage(&msg);
    }
#ifdef AVOX_ENABLE_FREETYPE
    // 每秒更新一次时间戳（block 2）
    auto now = std::chrono::steady_clock::now();
    if (fontLayer &&
        std::chrono::duration_cast<std::chrono::seconds>(now - lastTimeUpdate)
                .count() >= 1) {
      auto sysTime = std::chrono::system_clock::to_time_t(
          std::chrono::system_clock::now());
      char timeStr[64];
      strftime(timeStr, sizeof(timeStr), "%H:%M:%S", localtime(&sysTime));
      fontLayer->setTextLayout(2);
      fontLayer->drawText(timeStr);
      lastTimeUpdate = now;
    }
#endif
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  mp->close();
  return 0;
}
