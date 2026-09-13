#include <cstdio>
#include <cstring>

#include "avox/module/AvoxManager.hpp"
#include "avox/module/ModuleMgr.hpp"
#include "avox/subtitle/IAssOverlay.hpp"

using namespace avox;

// avox_ass 插件冒烟:
// 场景 A(正常): avox_ass.dll 在 avox.dll 同级 plugins/ → create 返回实例。
//   - 骨架模式(未链 libass): init() 返回 false → 降级路径;
//   - 真实模式(AVOX_ASS_DEPS_DIR 链上 libass): init() true → 写最小 .ass →
//     loadFile → render 期望非空 canvas。
// 场景 B(缺失): 把 avox_ass.dll 改名后再跑 → create==nullptr → 降级, 退出码 0。
static const char* kSampleAss =
    "[Script Info]\nScriptType: v4.00+\nPlayResX: 1920\nPlayResY: 1080\n\n"
    "[V4+ Styles]\nFormat: Name, Fontname, Fontsize, PrimaryColour, OutlineColour, "
    "BackColour, Bold, Italic, BorderStyle, Outline, Shadow, Alignment, MarginL, "
    "MarginR, MarginV, Encoding\n"
    "Style: Default,Arial,54,&H00FFFFFF,&H00000000,&H00000000,0,0,1,2,0,2,60,60,40,1\n\n"
    "[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
    "Dialogue: 0,0:00:00.00,0:01:00.00,Default,,0,0,0,,Hello ASS overlay\n";

int main() {
  int failed = 0;

  IAssOverlay* overlay = AvoxManager::Get().assOverlayHub.create("libass");
  if (!overlay) {
    std::printf(
        "[asstest] create==nullptr(avox_ass 插件缺失/未加载) → 降级路径 OK\n");
    return 0;
  }
  std::printf("[asstest] 插件已加载, assOverlayHub 工厂返回实例\n");

  const bool initOk = overlay->init(1920, 1080);
  std::printf("[asstest] init(1920x1080) = %s (%s)\n", initOk ? "true" : "false",
              initOk ? "libass 已链" : "骨架模式, 降级路径 OK");

  if (initOk) {
    // 场景 C: 真实渲染 → 非空 canvas
    const char* fonts[] = {"C:\\Windows\\Fonts\\msyh.ttc",
                           "C:\\Windows\\Fonts\\arial.ttf",
                           "C:\\Windows\\Fonts\\segoeui.ttf"};
    bool haveFont = false;
    for (const char* f : fonts) {
      FILE* t = std::fopen(f, "rb");
      if (t) {
        std::fclose(t);
        overlay->setDefaultFont(f, "Arial");
        haveFont = true;
        break;
      }
    }
    if (!haveFont) std::printf("[asstest] 未找到系统字体, 跳过渲染断言\n");

    FILE* af = std::fopen("asstest_sample.ass", "wb");
    if (af) {
      std::fwrite(kSampleAss, 1, std::strlen(kSampleAss), af);
      std::fclose(af);
    }
    if (haveFont && af && overlay->loadFile("asstest_sample.ass")) {
      const AssCanvas* c = overlay->render(1000);
      if (c && c->rgba && c->width > 0 && c->height > 0 && c->seq > 0) {
        std::printf("[asstest] render OK: canvas %dx%d @(%d,%d) seq=%u\n", c->width,
                    c->height, c->x, c->y, unsigned(c->seq));
        // 像素证据: 合成到黑底导出 PPM, 供人眼验收字形/描边
        FILE* dump = std::fopen("asstest_canvas.ppm", "wb");
        if (dump) {
          std::fprintf(dump, "P6\n%d %d\n255\n", c->width, c->height);
          for (int32_t y = 0; y < c->height; ++y) {
            const uint8_t* row = c->rgba + size_t(y) * c->stride;
            for (int32_t x = 0; x < c->width; ++x) {
              const uint8_t* px = row + size_t(x) * 4;
              uint8_t rgb[3] = {uint8_t(px[0] * px[3] / 255),
                                uint8_t(px[1] * px[3] / 255),
                                uint8_t(px[2] * px[3] / 255)};
              std::fwrite(rgb, 1, 3, dump);
            }
          }
          std::fclose(dump);
          std::printf("[asstest] canvas 已导出 asstest_canvas.ppm\n");
        }
      } else {
        std::printf("[asstest] FAIL: render 应返回非空 canvas\n");
        failed++;
      }
      const AssCanvas* c2 = overlay->render(2000);
      if (c2 && c2->seq == c->seq) {
        std::printf("[asstest] detect_change 稳定段: seq 不变, 跳过重复上传 OK\n");
      }
    } else if (haveFont && af) {
      std::printf("[asstest] FAIL: loadFile 返回 false\n");
      failed++;
    }
    std::remove("asstest_sample.ass");
  }

  overlay->shutdown();
  delete overlay;

  if (failed) {
    std::printf("[asstest] FAILED: %d 项\n", failed);
    return 1;
  }
  std::printf("[asstest] ALL OK\n");
  return 0;
}
