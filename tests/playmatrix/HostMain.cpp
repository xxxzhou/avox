// 播放回归矩阵的通用宿主 (命令行): 各平台共用同一份, 只做参数解析 + 资源定位 + 调 runAll
//
// 用例表与判定口径在 tests/playmatrix/PlayMatrix.hpp, 各平台只是"怎么把它编出来":
//   Windows          platform/windows/playtest  (console exe, 无头离屏)
//   Android          platform/android/playtest  (console 可执行, adb push + adb shell 跑)
//   Linux            platform/linux/playtest    (console exe)
//   Apple            platform/ios/avoxtest      (iOS app + macOS 无头 CLI, 自带宿主)
// 退出码 0=全过, 可接 CI。一键驱动见 script/testenv/play_regress.py
//
// 用法:
//   playtest                                  # 默认 127.0.0.1 + 仓库 assets/video 本地源
//   playtest --host=192.168.68.245            # 拉局域网另一台 ZLM
//   playtest --skip=webrtc-h265,shot          # 跳过指定用例
//   playtest --all                            # 连 enabled=false 的用例一起跑
//   playtest --win                            # 界面走查: 出窗口顺序渲染 + 左上角说明横幅,
//                                             # 人按横幅说明判画面; 关窗即停矩阵 (仅 Windows/macOS)
//   playtest --log=my.log                     # stdout 镜像到日志文件 (默认 <prefix>log.txt)
//   playtest --list                           # 只列用例表
// 环境变量: AVOX_PM_HOST / AVOX_PM_OUT / AVOX_PM_ASSET_DIR

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#define NOMINMAX  // windows.h 的 min/max 宏会打断 std::max/std::min
#include <windows.h>
#endif

#include "avox/module/AvoxManager.hpp"

#include "playmatrix/PlayMatrix.hpp"
#include "playmatrix/PlayTee.hpp"

using namespace avox;
using namespace avox::playmatrix;

namespace {

// 逐级上溯找本地源 (Windows: 可执行目录往上有仓库 assets; Android: /data/local/tmp)
std::string findAsset(const std::string& name) {
  std::vector<std::string> roots;
  if (const char* envDir = getenv("AVOX_PM_ASSET_DIR")) {
    roots.push_back(envDir);
  }
#if defined(__ANDROID__)
  roots.push_back("/data/local/tmp/playmatrix/assets/video");
  roots.push_back("/data/local/tmp/playmatrix");
#endif
  std::string up;
  for (int i = 0; i < 7; ++i) {
    roots.push_back(up + "assets/video");
    up += "../";
  }
  for (const std::string& root : roots) {
    std::string path = root + "/" + name;
    if (fileSize(path) > 0) {
      return path;
    }
  }
  return std::string();
}

std::string argValue(const std::string& arg, const char* key) {
  size_t len = std::strlen(key);
  return arg.compare(0, len, key) == 0 ? arg.substr(len) : std::string();
}

std::vector<std::string> splitComma(const std::string& text) {
  std::vector<std::string> out;
  size_t start = 0;
  while (start <= text.size()) {
    size_t pos = text.find(',', start);
    std::string item =
        text.substr(start, pos == std::string::npos ? std::string::npos : pos - start);
    if (!item.empty()) {
      out.push_back(item);
    }
    if (pos == std::string::npos) {
      break;
    }
    start = pos + 1;
  }
  return out;
}

const char* platformName() {
#if defined(_WIN32)
  return "win";
#elif defined(__ANDROID__)
  return "and";
#elif defined(__APPLE__)
  return "apple";
#else
  return "linux";
#endif
}

}  // namespace

#if defined(_WIN32)
namespace {

// ── --win 界面走查横幅 (主窗口左上角) ──
// 每条用例开始时显示 [i/N] id + desc(该看到什么, 来自用例表), 底部滚最近 2 条判定行;
// 人照着说明判画面是否正常。判定行口径不变, 只是"有人看"的那一路多了块字幕牌。
// 线程模型: 走查线程 (runAll 所在) 只改文本 + PostMessage; GDI 绘制全在 UI 线程做。
struct WalkBanner {
  std::mutex mtx;
  std::string head = "avoxtest 界面走查 — 等待用例…";
  std::string desc = "拉流用例的画面出在本窗口; 离屏用例(截图/录制/字幕取证)无画面。"
                     "随时关窗终止矩阵";
  std::vector<std::string> verdicts;  // 最近 2 条 [AVOX][TEST] 判定行
  std::atomic<bool> cancel{false};    // 关窗置位 → runAll 下一条前退出
  HWND hwnd = nullptr;

  void setCase(const PlayCase& c, int32_t idx, int32_t total) {
    std::lock_guard<std::mutex> l(mtx);
    head = "[" + std::to_string(idx + 1) + "/" + std::to_string(total) + "] " + c.id +
           "  ● 运行中";
    desc = c.desc.empty() ? c.url : c.desc;
    post();
  }
  void pushVerdict(const std::string& line) {
    std::lock_guard<std::mutex> l(mtx);
    verdicts.push_back(line);
    if (verdicts.size() > 2) {
      verdicts.erase(verdicts.begin());
    }
    post();
  }
  void post() {
    if (hwnd) {
      PostMessageW(hwnd, kRedraw, 0, 0);
    }
  }
  static constexpr UINT kRedraw = WM_APP + 1;
};

WalkBanner* g_walk = nullptr;  // 单实例宿主, 文件级指针即可

std::wstring utf8Wide(const std::string& s) {
  int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
  std::wstring w(n, L'\0');
  if (n > 0) {
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
  }
  return w;
}

HFONT makeBannerFont(int height, int weight) {
  return CreateFontW(height, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                     OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                     DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
}

// 横幅重绘: 32bpp premultiplied DIB 上画文本 → UpdateLayeredWindow (半透明黑底白字)。
// GDI 不写 alpha 字节, 文本像素会留着底色 alpha → 末尾扫一遍: RGB 非 0 即置不透明。
// ULW 失败的环境 (个别驱动/远程会话) 自动退回: 整窗均匀半透明 (SLWA) + 普通 GDI BitBlt
namespace {
bool g_plainBanner = false;  // ULW 失败后的回退绘制模式
}

void drawBannerContent(HDC mem, void* bits, int w, int h, const std::string& head,
                       const std::string& desc, const std::vector<std::string>& verdicts) {
  // 背景: premultiplied 半透明黑 (RGB=0 合法于任意 alpha; BitBlt 路径 alpha 被忽略)
  for (int y = 0; y < h; ++y) {
    uint32_t* row = (uint32_t*)((uint8_t*)bits + (size_t)y * 4 * w);
    for (int x = 0; x < w; ++x) {
      row[x] = 0xA8000000u;  // A=168
    }
  }
  HFONT fHead = makeBannerFont(-26, FW_SEMIBOLD);
  HFONT fDesc = makeBannerFont(-22, FW_NORMAL);
  HFONT fLine = makeBannerFont(-19, FW_NORMAL);
  SetBkMode(mem, TRANSPARENT);
  int y = 8;
  SelectObject(mem, fHead);
  SetTextColor(mem, RGB(255, 255, 255));
  std::wstring wh = utf8Wide(head);
  RECT rh = {10, y, w - 10, y + 34};
  DrawTextW(mem, wh.c_str(), (int)wh.size(), &rh,
            DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
  y += 36;
  SelectObject(mem, fDesc);
  SetTextColor(mem, RGB(236, 241, 248));
  std::wstring wd = utf8Wide(desc);
  RECT rm = {10, y, w - 10, y + 120};
  DrawTextW(mem, wd.c_str(), (int)wd.size(), &rm,
            DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);  // 先量高
  RECT rd = {10, y, w - 10, rm.bottom};
  DrawTextW(mem, wd.c_str(), (int)wd.size(), &rd, DT_WORDBREAK | DT_NOPREFIX);
  y = rm.bottom + 4;
  SelectObject(mem, fLine);
  SetTextColor(mem, RGB(172, 192, 216));
  for (const std::string& v : verdicts) {
    RECT rv = {10, y, w - 10, y + 26};
    std::wstring wv = utf8Wide(v);
    DrawTextW(mem, wv.c_str(), (int)wv.size(), &rv,
              DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    y += 24;
  }
  // alpha 修正: GDI 只写了 RGB, 把文本像素置为不透明 (抗锯齿暗边即"压实的字色")
  for (int yy = 0; yy < h; ++yy) {
    uint32_t* row = (uint32_t*)((uint8_t*)bits + (size_t)yy * 4 * w);
    for (int xx = 0; xx < w; ++xx) {
      if (row[xx] & 0x00FFFFFFu) {
        row[xx] |= 0xFF000000u;
      }
    }
  }
  DeleteObject(fHead);
  DeleteObject(fDesc);
  DeleteObject(fLine);
}

void paintWalkBanner(HWND hb) {
  if (!g_walk) {
    return;
  }
  std::string head;
  std::string desc;
  std::vector<std::string> verdicts;
  {
    std::lock_guard<std::mutex> l(g_walk->mtx);
    head = g_walk->head;
    desc = g_walk->desc;
    verdicts = g_walk->verdicts;
  }
  RECT rc;
  GetClientRect(hb, &rc);
  int w = rc.right;
  int h = rc.bottom;
  if (w <= 16 || h <= 16) {
    return;
  }
  HDC screen = GetDC(nullptr);
  HDC mem = CreateCompatibleDC(screen);
  BITMAPINFO bmi = {};
  bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bmi.bmiHeader.biWidth = w;
  bmi.bmiHeader.biHeight = -h;  // top-down
  bmi.bmiHeader.biPlanes = 1;
  bmi.bmiHeader.biBitCount = 32;
  bmi.bmiHeader.biCompression = BI_RGB;
  void* bits = nullptr;
  HBITMAP dib = CreateDIBSection(screen, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
  if (!dib || !bits) {
    std::printf("[warn] walk banner: CreateDIBSection failed\n");
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
    return;
  }
  HGDIOBJ oldBmp = SelectObject(mem, dib);
  drawBannerContent(mem, bits, w, h, head, desc, verdicts);
  if (g_plainBanner) {
    // 回退: 普通 GDI 上屏 (SLWA 整窗均匀半透明已在首次失败时设置)
    HDC wdc = GetDC(hb);
    BitBlt(wdc, 0, 0, w, h, mem, 0, 0, SRCCOPY);
    ReleaseDC(hb, wdc);
  } else {
    POINT dst = {0, 0};
    SIZE size = {w, h};
    POINT src = {0, 0};
    BLENDFUNCTION bf = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    if (!UpdateLayeredWindow(hb, nullptr, &dst, &size, mem, &src, 0, &bf, ULW_ALPHA)) {
      // 分层子窗口 ULW 不可用 → 整窗均匀半透明 + 普通 GDI, 内容照样出
      g_plainBanner = true;
      SetLayeredWindowAttributes(hb, 0, 235, LWA_ALPHA);
      std::printf("[warn] walk banner: ULW failed (err=%lu), fallback plain GDI\n",
                  (unsigned long)GetLastError());
      HDC wdc = GetDC(hb);
      BitBlt(wdc, 0, 0, w, h, mem, 0, 0, SRCCOPY);
      ReleaseDC(hb, wdc);
    }
  }
  SelectObject(mem, oldBmp);
  DeleteObject(dib);
  DeleteDC(mem);
  ReleaseDC(nullptr, screen);
  ValidateRect(hb, nullptr);
}

}  // namespace
#endif  // _WIN32

#if defined(__APPLE__)
namespace avox {
namespace playmatrix {
// macOS 窗口宿主 (platform/macos/playtest/WinHost.mm): AppKit 窗口出画面,
// tee.onLine 喂判定横幅; 返回矩阵退出码
int runAppleWindowHost(const std::vector<PlayCase>& cases, const RunOptions& opt,
                       StdoutTee* tee);
}
}
#endif

int main(int argc, char* argv[]) {
  Endpoints ep;
  RunOptions opt;
  bool listOnly = false;
  bool winMode = false;
  StdoutTee tee;
  std::string logPath;
  if (const char* v = getenv("AVOX_PM_HOST")) ep.host = v;
  if (const char* v = getenv("AVOX_PM_OUT")) opt.outDir = v;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    std::string v;
    if (!(v = argValue(a, "--host=")).empty()) ep.host = v;
    else if (!(v = argValue(a, "--rtsp=")).empty()) ep.rtspPort = std::atoi(v.c_str());
    else if (!(v = argValue(a, "--rtmp=")).empty()) ep.rtmpPort = std::atoi(v.c_str());
    else if (!(v = argValue(a, "--http=")).empty()) ep.httpPort = std::atoi(v.c_str());
    else if (!(v = argValue(a, "--h264=")).empty()) ep.h264Key = v;
    else if (!(v = argValue(a, "--h265=")).empty()) ep.h265Key = v;
    else if (!(v = argValue(a, "--file-h264=")).empty()) ep.fileH264 = v;
    else if (!(v = argValue(a, "--file-h265=")).empty()) ep.fileH265 = v;
    else if (!(v = argValue(a, "--file-hdr10=")).empty()) ep.fileHdr10 = v;
    else if (!(v = argValue(a, "--file-hdr10-aud=")).empty()) ep.fileHdr10Aud = v;
    else if (!(v = argValue(a, "--file-sub-bg=")).empty()) ep.fileSubBg = v;
    else if (!(v = argValue(a, "--file-sub-srt-utf8=")).empty()) ep.fileSubSrtUtf8 = v;
    else if (!(v = argValue(a, "--file-sub-srt-bom=")).empty()) ep.fileSubSrtBom = v;
    else if (!(v = argValue(a, "--file-sub-srt-gbk=")).empty()) ep.fileSubSrtGbk = v;
    else if (!(v = argValue(a, "--file-sub-srt-rich=")).empty()) ep.fileSubSrtRich = v;
    else if (!(v = argValue(a, "--file-sub-ass=")).empty()) ep.fileSubAss = v;
    else if (!(v = argValue(a, "--file-sub-srt-embed=")).empty()) ep.fileSubSrtEmbed = v;
    else if (!(v = argValue(a, "--file-sub-ass-embed=")).empty()) ep.fileSubAssEmbed = v;
    else if (!(v = argValue(a, "--file-sub-movtext=")).empty()) ep.fileSubMovText = v;
    else if (!(v = argValue(a, "--outdir=")).empty()) opt.outDir = v;
    else if (!(v = argValue(a, "--prefix=")).empty()) opt.prefix = v;
    else if (!(v = argValue(a, "--retries=")).empty()) opt.retries = std::atoi(v.c_str());
    else if (!(v = argValue(a, "--skip=")).empty()) opt.skip = splitComma(v);
    else if (!(v = argValue(a, "--only=")).empty()) opt.only = v;
    else if (!(v = argValue(a, "--log=")).empty()) logPath = v;
    else if (a == "--all") opt.includeDisabled = true;
    else if (a == "--win") winMode = true;
    else if (a == "--list") listOnly = true;
    else std::printf("[warn] unknown arg: %s\n", a.c_str());
  }
  // 本地源缺省用标准测试源 (assets/video, testsrc2 图案+烧录时间码)。
  // 注: 迁移到 avox-test 后 test/ 子目录已摊平到 assets/video/ 根, 不再有嵌套。
  if (ep.fileH264.empty()) {
    ep.fileH264 = findAsset("test_h264_aac_640x360.mp4");
  }
  if (ep.fileH265.empty()) {
    ep.fileH265 = findAsset("test_h265_aac_960x540.mp4");
  }
  if (ep.fileHdr10.empty()) {
    ep.fileHdr10 = findAsset("test_h265_hdr10_pq_640x360.mp4");
  }
  if (ep.fileHdr10Aud.empty()) {
    ep.fileHdr10Aud = findAsset("test_h265_hdr10_pq_640x360_aud.mp4");
  }
  // 字幕资产默认用仓库 assets/video 下的自生成样本 (gen_subtitle.py)
  if (ep.fileSubBg.empty())        ep.fileSubBg = findAsset("sub_bg_640x360.mp4");
  if (ep.fileSubSrtUtf8.empty())   ep.fileSubSrtUtf8 = findAsset("sub_srt_utf8.srt");
  if (ep.fileSubSrtBom.empty())    ep.fileSubSrtBom = findAsset("sub_srt_bom.srt");
  if (ep.fileSubSrtGbk.empty())    ep.fileSubSrtGbk = findAsset("sub_srt_gbk.srt");
  if (ep.fileSubSrtRich.empty())   ep.fileSubSrtRich = findAsset("sub_srt_rich.srt");
  if (ep.fileSubAss.empty())       ep.fileSubAss = findAsset("sub_ass_pos.ass");
  if (ep.fileSubSrtEmbed.empty())  ep.fileSubSrtEmbed = findAsset("sub_srt_embed.mkv");
  if (ep.fileSubAssEmbed.empty())  ep.fileSubAssEmbed = findAsset("sub_ass_embed.mkv");
  if (ep.fileSubMovText.empty())   ep.fileSubMovText = findAsset("sub_movtext.mp4");
  std::vector<PlayCase> cases = buildCases(ep);
  // 产物目录不存在时截图/录制会连环失败 (saveImagePath/avio_open2 都不建目录)
  if (!opt.outDir.empty()) {
    std::error_code ec;
    std::filesystem::create_directories(opt.outDir, ec);
  }
  if (listOnly) {
    std::printf("platform=%s host=%s rtsp=%d rtmp=%d http=%d h264=%s h265=%s\n",
                platformName(), ep.host.c_str(), ep.rtspPort, ep.rtmpPort, ep.httpPort,
                ep.h264Key.c_str(), ep.h265Key.c_str());
  std::printf("file-h264=%s\nfile-h265=%s\nfile-hdr10=%s\nfile-hdr10-aud=%s\n",
              ep.fileH264.c_str(), ep.fileH265.c_str(), ep.fileHdr10.c_str(),
              ep.fileHdr10Aud.c_str());
  std::printf("sub-bg=%s\nsub-srt-utf8=%s\nsub-srt-bom=%s\nsub-srt-gbk=%s\n"
              "sub-srt-rich=%s\nsub-ass=%s\nsub-srt-embed=%s\nsub-ass-embed=%s\n"
              "sub-movtext=%s\n\n",
              ep.fileSubBg.c_str(), ep.fileSubSrtUtf8.c_str(), ep.fileSubSrtBom.c_str(),
              ep.fileSubSrtGbk.c_str(), ep.fileSubSrtRich.c_str(), ep.fileSubAss.c_str(),
              ep.fileSubSrtEmbed.c_str(), ep.fileSubAssEmbed.c_str(),
              ep.fileSubMovText.c_str());
    printCases(cases);
    return 0;
  }
  // 日志落盘: 默认 <outDir>/<prefix>log.txt, --log= 覆盖; 失败不阻断矩阵
  if (logPath.empty()) {
    logPath = joinPath(opt.outDir, opt.prefix + "log.txt");
  }
  if (tee.start(logPath)) {
    std::printf("[info] log file: %s\n", logPath.c_str());
  }
  // Android console 进程没有 JNI_OnLoad, 手动触发模块注册 (Windows 走 DllMain 已注册, bInit 幂等)
  AvoxManager::Get().init();
  std::printf("[AVOX][TEST] case=play-matrix-start result=PASS platform=%s host=%s "
              "cases=%d\n",
              platformName(), ep.host.c_str(), (int)cases.size());
#if defined(_WIN32)
  // --win 界面走查: 出窗口顺序渲染各拉流用例画面, 左上角横幅(WalkBanner)显示
  // 当前用例 + 该看到什么 + 最近判定行; 矩阵跑后台线程, 主线程泵消息防 Not Responding。
  // 关窗 = WM_DESTROY 置 cancel → 矩阵当前用例跑完即停, 不再吊着人等完全表
  if (winMode) {
    WalkBanner walk;
    g_walk = &walk;
    WNDCLASSW wc = {};
    wc.lpfnWndProc = [](HWND h, UINT m, WPARAM w, LPARAM l) -> LRESULT {
      if (m == WM_DESTROY) {
        if (g_walk) {
          g_walk->cancel = true;  // 关窗即取消: 剩余用例不再开跑
        }
        PostQuitMessage(0);
        return 0;
      }
      return DefWindowProcW(h, m, w, l);
    };
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"avoxtest_play";
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));  // IDC_ARROW (非UNICODE构建无W宏)
    RegisterClassW(&wc);
    // 走查横幅: 分层子窗口 (Win8+ 支持), 左上角钉住; WM_PAINT/重绘消息都走 paintWalkBanner
    WNDCLASSW wb = {};
    wb.lpfnWndProc = [](HWND h, UINT m, WPARAM w, LPARAM l) -> LRESULT {
      if (m == WM_PAINT || (g_walk && m == WalkBanner::kRedraw)) {
        paintWalkBanner(h);
        return 0;
      }
      return DefWindowProcW(h, m, w, l);
    };
    wb.hInstance = wc.hInstance;
    wb.lpszClassName = L"avoxtest_pmbanner";
    wb.hCursor = wc.hCursor;
    RegisterClassW(&wb);
    HWND hwnd = CreateWindowW(wc.lpszClassName, L"avoxtest play matrix",
                              WS_OVERLAPPEDWINDOW | WS_VISIBLE, 60, 60, 960, 540,
                              nullptr, nullptr, wc.hInstance, nullptr);
    if (!hwnd) {
      std::printf("[warn] create window failed, fallback offscreen\n");
      winMode = false;
    } else {
      walk.hwnd = CreateWindowExW(WS_EX_LAYERED, wb.lpszClassName, L"",
                                  WS_CHILD | WS_VISIBLE, 0, 0, 680, 200, hwnd, nullptr,
                                  wc.hInstance, nullptr);
      if (!walk.hwnd) {
        // 个别环境 (RDP/远程会话) 对分层子窗口创建直接拒绝: 退普通子窗口 +
        // 整块不透明 GDI 绘制 (paintWalkBanner 的 g_plainBanner 路径)
        std::printf("[warn] walk banner: layered create failed (err=%lu), plain child\n",
                    (unsigned long)GetLastError());
        walk.hwnd = CreateWindowW(wb.lpszClassName, L"", WS_CHILD | WS_VISIBLE, 0, 0,
                                  680, 200, hwnd, nullptr, wc.hInstance, nullptr);
        g_plainBanner = true;
      }
      if (!walk.hwnd) {
        std::printf("[warn] walk banner window create failed (err=%lu)\n",
                    (unsigned long)GetLastError());
      }
      // 判定行喂横幅 (tee 泵线程回调, 只改文本+投递, 不碰 GDI)
      tee.onLine = [](const std::string& line) {
        if (line.rfind("[AVOX][TEST]", 0) == 0 && g_walk) {
          g_walk->pushVerdict(line);
        }
      };
      RunOptions wopt = opt;  // 值拷贝补走查钩子, 无头回退路径仍用原 opt
      wopt.onCaseStart = [](const PlayCase& c, int32_t idx, int32_t total) {
        g_walk->setCase(c, idx, total);
      };
      wopt.cancel = &walk.cancel;
      int code = 0;
      std::thread runner([&] {
        code = runAll(cases, (void*)hwnd, wopt);
        // 矩阵跑完投递关闭: WM_CLOSE→DestroyWindow→WM_DESTROY→PostQuitMessage
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
      });
      MSG msg;
      while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
      }
      runner.join();
      tee.onLine = nullptr;
      g_walk = nullptr;
      DestroyWindow(hwnd);
      AvoxManager::clean();
      tee.stop();
      return code;
    }
  }
#endif
#if defined(__APPLE__)
  // --win: AppKit 窗口 (CAMetalLayer 画面 + 判定横幅), 与 Windows 宿主同参数;
  // 宿主内部泵主线程 runloop, 跑完返回退出码
  if (winMode) {
    int code = runAppleWindowHost(cases, opt, &tee);
    AvoxManager::clean();
    tee.stop();
    return code;
  }
#endif
  (void)winMode;
  int code = runAll(cases, nullptr, opt);
  // Android console 没有 DllMain(DETACH) 兜底, 退出前显式有序清理
  // (mk_env_release 等 cleanFuncs), 否则 libmk_api 静态析构序倒挂退出必崩
  AvoxManager::clean();
  tee.stop();
  return code;
}
