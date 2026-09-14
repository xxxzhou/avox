#include <cstdio>
#include <cstring>

#include "avox/module/AvoxManager.hpp"
#include "avox/module/ModuleMgr.hpp"
#include "avox/subtitle/IAssOverlay.hpp"

using namespace avox;

// \pos 定位探针(插件级, 脱离播放管线): 同 ass_test.mkv 的剧本头 + 两条对白,
// 验证 libass 画布 bbox 位置 —— 对白2 \pos(320,60) 底边锚点 → 画布应出现在
// 画面顶部(y≈0~20); 对白1 \pos(320,320) → 底部(y≈260~320)。用于区分
// 「libass/插件定位问题」与「播放管线(时钟/喂包)问题」。
static const char* kHeader =
    "[Script Info]\nScriptType: v4.00+\nPlayResX: 640\nPlayResY: 360\n\n"
    "[V4+ Styles]\nFormat: Name, Fontname, Fontsize, PrimaryColour, "
    "OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, "
    "ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, "
    "MarginL, MarginR, MarginV, Encoding\n"
    "Style: Default,Arial,42,&H00FFFFFF,&H00000000,&H00000000,0,0,0,0,100,"
    "100,0,0,1,2,0,2,20,20,20,1\n";

static const char* kEvents =
    "[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, "
    "MarginV, Effect, Text\n";

static void feed(IAssOverlay* ov, int readOrder, int layer, const char* text,
                 int64_t pts, int64_t dur) {
  char line[512];
  std::snprintf(line, sizeof(line), "%d,%d,Default,,0,0,0,,%s", readOrder,
                layer, text);
  ov->processChunk(line, (int32_t)std::strlen(line), pts, dur);
}

int main() {
  ModuleMgr::Get().ensureStarted();
  IAssOverlay* ov = AvoxManager::Get().assOverlayHub.create("libass");
  if (!ov) {
    std::printf("case=asspos FAIL (no plugin)\n");
    return 1;
  }
  const char* fonts[][2] = {
      {"C:\\Windows\\Fonts\\arial.ttf", "Arial"},
      {"C:\\Windows\\Fonts\\msyh.ttc", "Microsoft YaHei"},
  };
  for (const auto& f : fonts) {
    FILE* t = std::fopen(f[0], "rb");
    if (t) {
      std::fclose(t);
      ov->setDefaultFont(f[0], f[1]);
      break;
    }
  }
  if (!ov->init(640, 360)) {
    std::printf("case=asspos FAIL (init)\n");
    return 1;
  }
  std::string script = std::string(kHeader) + kEvents;
  if (!ov->loadTrack(script.c_str(), (int32_t)script.size())) {
    std::printf("case=asspos FAIL (loadTrack)\n");
    return 1;
  }
  // 对白1: 0~30s 底部; 对白2: 5~20s 顶部
  feed(ov, 0, 0, "{\\pos(320,320)}{\\c&H00FF00&}ASS overlay demo", 0, 30000);
  feed(ov, 1, 0, "{\\pos(320,60)}Hello {\\fs30}ASS {\\t(0,2000,\\fs60)}track",
       5000, 15000);

  int failed = 0;
  struct Case {
    int64_t pts;
    const char* name;
    int yMin, yMax;  // 期望画布 y 范围
    bool wantTop;
  };
  // pts=1000: 只有对白1(底部, 画布 y≈270~330); pts=6000: 两层联合(顶部到
  // 底部, 联合 bbox 覆盖大部分画面); pts=6000 单看对白2 需隔离——先看联合。
  Case cases[] = {
      {1000, "d1-bottom", 240, 340, false},
      {6000, "d1+d2", 0, 340, true},
  };
  for (auto& c : cases) {
    const AssCanvas* cv = ov->render(c.pts);
    if (!cv || !cv->rgba) {
      std::printf("[asspos] %s: empty canvas (unexpected)\n", c.name);
      failed++;
      continue;
    }
    std::printf("[asspos] %s pts=%lld canvas %dx%d @(%d,%d)\n", c.name,
                (long long)c.pts, cv->width, cv->height, cv->x, cv->y);
    const bool topHit = cv->y < 100;  // 画布顶到上半屏 = \pos(320,60) 生效
    if (c.wantTop && !topHit) {
      std::printf("[asspos] FAIL: 联合画布未覆盖顶部, \\pos(320,60) 未生效\n");
      failed++;
    }
    if (!c.wantTop && (cv->y < c.yMin || cv->y > c.yMax)) {
      std::printf("[asspos] FAIL: 底部对白画布 y=%d 不在 [%d,%d]\n", cv->y,
                  c.yMin, c.yMax);
      failed++;
    }
  }
  // 单独验证对白2: 只喂它, pts 落在其区间
  ov->flush();
  feed(ov, 2, 0, "{\\pos(320,60)}Hello {\\fs30}ASS {\\t(0,2000,\\fs60)}track",
       5000, 15000);
  const AssCanvas* cv = ov->render(6000);
  if (!cv || !cv->rgba) {
    std::printf("[asspos] FAIL: d2-only empty canvas\n");
    failed++;
  } else {
    std::printf("[asspos] d2-only canvas %dx%d @(%d,%d)\n", cv->width,
                cv->height, cv->x, cv->y);
    if (cv->y > 100) {
      std::printf("[asspos] FAIL: \\pos(320,60) 画布 y=%d 应在顶部(<100)\n",
                  cv->y);
      failed++;
    }
  }
  std::printf("[AVOX][TEST] case=asspos result=%s\n", failed ? "FAIL" : "PASS");
  return failed ? 1 : 0;
}
