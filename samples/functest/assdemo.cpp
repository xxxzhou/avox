#include <cstdio>
#include <cstring>
#include <windows.h>

#include "avox/module/AvoxManager.hpp"
#include "avox/module/ModuleMgr.hpp"
#include "avox/subtitle/IAssOverlay.hpp"

using namespace avox;

// 首轮异常即打印: 出错指令所在模块 + 访问的目标地址 (定位跨模块崩点)
static LONG WINAPI crashReporter(EXCEPTION_POINTERS* e) {
  if (e->ExceptionRecord->ExceptionCode == 0xC0000005) {
    HMODULE m = nullptr;
    char mod[MAX_PATH] = "(unknown)";
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCSTR)e->ExceptionRecord->ExceptionAddress, &m);
    GetModuleFileNameA(m, mod, MAX_PATH);
    HMODULE base = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCSTR)e->ExceptionRecord->ExceptionAddress, &m);
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, mod, &base);
    std::fprintf(stderr, "[veh] ACCESS_VIOLATION op=%llu ip=%p mod=%s base=%p "
                 "access-addr=%p\n",
                 (unsigned long long)e->ExceptionRecord->ExceptionInformation[0],
                 e->ExceptionRecord->ExceptionAddress, mod, (void*)base,
                 (void*)(e->ExceptionRecord->ExceptionInformation[1]));
    // 崩溃地址归属诊断: 打印所在分配区
    MEMORY_BASIC_INFORMATION mbi{};
    if (VirtualQuery((LPCVOID)e->ExceptionRecord->ExceptionInformation[1], &mbi,
                     sizeof(mbi))) {
      std::fprintf(stderr, "[veh] fault region: alloc-base=%p state=0x%lx "
                   "type=0x%lx size=%zu\n",
                   mbi.AllocationBase, mbi.State, mbi.Type, mbi.RegionSize);
    }
    std::fflush(stderr);
  }
  return EXCEPTION_CONTINUE_SEARCH;
}

// ASS 字幕叠加演示: 读取 base.raw 背景帧(由 assdemo_genbase.py 生成),
// 经 avox_ass 插件(libass)逐帧渲染 demo.ass, src-over 合成后写 comp.raw,
// 再用 ffmpeg 编码为 mp4:
//   ffmpeg -f rawvideo -pix_fmt rgb24 -s 1280x720 -r 30 -i comp.raw -c:v libx264 -pix_fmt yuv420p ass_demo.mp4
// 演示效果: \move/\fad 入场、\kf 卡拉OK逐字高亮、\t 颜色/缩放动画、\pos 定位、
// 描边+阴影+换行双语 —— 全部为 libass 本地光栅化, 无转码无网络。
static const char* kDemoAss =
    "[Script Info]\n"
    "ScriptType: v4.00+\n"
    "PlayResX: 1280\n"
    "PlayResY: 720\n"
    "ScaledBorderAndShadow: yes\n"
    "\n"
    "[V4+ Styles]\n"
    "Format: Name, Fontname, Fontsize, PrimaryColour, OutlineColour, BackColour, "
    "Bold, Italic, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, "
    "MarginV, Encoding\n"
    "Style: Title,Microsoft YaHei,58,&H0020FFFF,&H00201040,&H00000000,1,0,1,3,1,8,60,60,30,1\n"
    "Style: Dialog,Microsoft YaHei,52,&H00FFFFFF,&H00000000,&H00000000,0,0,1,2,1,2,60,60,42,1\n"
    "Style: Note,Microsoft YaHei,34,&H0000E1FF,&H00000000,&H00000000,0,1,1,2,0,7,40,40,20,1\n"
    "\n"
    "[Events]\n"
    "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
    "Dialogue: 0,0:00:00.00,0:00:10.00,Title,,0,0,0,,"
    "{\\fad(600,600)\\move(640,150,640,90,0,600)}ASS \\N特效字幕\n"
    "Dialogue: 0,0:00:00.40,0:00:05.00,Dialog,,0,0,0,,"
    "{\\fad(400,400)}这是 libass 在本地完整渲染的双语字幕\n"
    "Dialogue: 0,0:00:00.40,0:00:05.00,Dialog,,0,0,0,,"
    "{\\fad(400,400)\\fs44}No more subtitle hunting — rendered on device\n"
    "Dialogue: 0,0:00:00.80,0:00:05.00,Dialog,,0,0,0,,"
    "{\\kf45}卡{\kf45}拉{\kf45}O{\kf45}K{\kf45}逐{\kf45}字{\kf45}高{\kf45}亮\n"
    "Dialogue: 0,0:00:05.20,0:00:10.00,Dialog,,0,0,0,,"
    "{\\fad(300,300)\\c&H00E1FF&\\t(0,800,\\fscx112\\fscy112)\\t(800,1600,\\fscx100\\fscy100)}"
    "任意{\\b1}颜色{\b0}、动画与{\\i1}样式{\i0}按 VSFilter 规范还原\n"
    "Dialogue: 0,0:00:01.50,0:00:08.50,Note,,0,0,0,,"
    "{\\pos(1180,150)\\fad(400,400)\\frz-6}avox_ass\n"
    "自研引擎本地渲染\n";

int main(int argc, char** argv) {
  AddVectoredExceptionHandler(1, crashReporter);
  const int W = 1280, H = 720, FPS = 30;
  const int frameBytes = W * H * 3;
  const int rowBytes = W * 3;
  const char* inPath = argc > 1 ? argv[1] : "base.raw";
  const char* outPath = argc > 2 ? argv[2] : "comp.raw";

  IAssOverlay* overlay = AvoxManager::Get().assOverlayHub.create("libass");
  if (!overlay) {
    std::printf("[assdemo] avox_ass 插件未加载\n");
    return 1;
  }
  const char* fonts[] = {"C:\\Windows\\Fonts\\msyh.ttc",
                         "C:\\Windows\\Fonts\\arial.ttf"};
  for (const char* f : fonts) {
    FILE* t = std::fopen(f, "rb");
    if (t) {
      std::fclose(t);
      overlay->setDefaultFont(f, "Microsoft YaHei");
      break;
    }
  }
  if (!overlay->init(W, H)) {
    std::printf("[assdemo] init 失败\n");
    return 1;
  }
  FILE* af = std::fopen("demo.ass", "wb");
  std::fwrite(kDemoAss, 1, std::strlen(kDemoAss), af);
  std::fclose(af);
  if (!overlay->loadFile("demo.ass")) {
    std::printf("[assdemo] loadFile 失败\n");
    return 1;
  }

  FILE* in = std::fopen(inPath, "rb");
  FILE* out = std::fopen(outPath, "wb");
  if (!in || !out) {
    std::printf("[assdemo] 打不开 %s / %s\n", inPath, outPath);
    return 1;
  }

  std::string bg(frameBytes, 0);
  int frames = 0;
  while (true) {
    size_t got = std::fread(bg.data(), 1, frameBytes, in);
    if (got < (size_t)frameBytes) break;
    const int64_t tMs = int64_t(frames) * 1000 / FPS;
    std::fprintf(stderr, "[dbg] f%d t=%lld read\n", frames, (long long)tMs);
    std::fflush(stderr);
    const AssCanvas* c = overlay->render(tMs);
    uint8_t* frame = reinterpret_cast<uint8_t*>(bg.data());
    if (c && c->rgba) {
      for (int32_t y = 0; y < c->height; ++y) {
        const uint8_t* src = c->rgba + size_t(y) * c->stride;
        uint8_t* dst = frame + size_t(c->y + y) * rowBytes + size_t(c->x) * 3;
        for (int32_t x = 0; x < c->width; ++x) {
          const uint32_t a = src[size_t(x) * 4 + 3];
          if (!a) continue;
          uint8_t* px = dst + size_t(x) * 3;
          px[0] = uint8_t((src[size_t(x) * 4] * a + px[0] * (255 - a)) / 255);
          px[1] = uint8_t((src[size_t(x) * 4 + 1] * a + px[1] * (255 - a)) / 255);
          px[2] = uint8_t((src[size_t(x) * 4 + 2] * a + px[2] * (255 - a)) / 255);
        }
      }
    }
    std::fwrite(frame, 1, frameBytes, out);
    std::fprintf(stderr, "[dbg] f%d written\n", frames);
    std::fflush(stderr);
    ++frames;
  }
  std::fclose(in);
  std::fclose(out);
  overlay->shutdown();
  delete overlay;
  std::printf("[assdemo] 合成完成: %d 帧 → %s\n", frames, outPath);
  std::printf("编码: ffmpeg -f rawvideo -pix_fmt rgb24 -s %dx%d -r %d -i %s "
              "-c:v libx264 -pix_fmt yuv420p ass_demo.mp4\n", W, H, FPS, outPath);
  return 0;
}
