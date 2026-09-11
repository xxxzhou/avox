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
//   playtest --win                            # 出窗口渲染画面 (仅 Windows), 判定行仍走 stdout
//   playtest --list                           # 只列用例表
// 环境变量: AVOX_PM_HOST / AVOX_PM_OUT / AVOX_PM_ASSET_DIR

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

int main(int argc, char* argv[]) {
  Endpoints ep;
  RunOptions opt;
  bool listOnly = false;
  bool winMode = false;
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
    else if (!(v = argValue(a, "--outdir=")).empty()) opt.outDir = v;
    else if (!(v = argValue(a, "--prefix=")).empty()) opt.prefix = v;
    else if (!(v = argValue(a, "--retries=")).empty()) opt.retries = std::atoi(v.c_str());
    else if (!(v = argValue(a, "--skip=")).empty()) opt.skip = splitComma(v);
    else if (a == "--all") opt.includeDisabled = true;
    else if (a == "--win") winMode = true;
    else if (a == "--list") listOnly = true;
    else std::printf("[warn] unknown arg: %s\n", a.c_str());
  }
  // 本地源缺省从仓库 assets/video 找 (h264 用 webrtc_pull, h265 用 avox_electron)
  if (ep.fileH264.empty()) {
    ep.fileH264 = findAsset("webrtc_pull.mp4");
  }
  if (ep.fileH265.empty()) {
    ep.fileH265 = findAsset("avox_electron.mp4");
  }
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
    std::printf("file-h264=%s\nfile-h265=%s\n\n", ep.fileH264.c_str(),
                ep.fileH265.c_str());
    printCases(cases);
    return 0;
  }
  // Android console 进程没有 JNI_OnLoad, 手动触发模块注册 (Windows 走 DllMain 已注册, bInit 幂等)
  AvoxManager::Get().init();
  std::printf("[AVOX][TEST] case=play-matrix-start result=PASS platform=%s host=%s "
              "cases=%d\n",
              platformName(), ep.host.c_str(), (int)cases.size());
#if defined(_WIN32)
  // --win: 出窗口顺序渲染各拉流用例画面; 矩阵跑后台线程, 主线程泵消息防 Not Responding
  if (winMode) {
    WNDCLASSW wc = {};
    wc.lpfnWndProc = [](HWND h, UINT m, WPARAM w, LPARAM l) -> LRESULT {
      if (m == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
      }
      return DefWindowProcW(h, m, w, l);
    };
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"avoxtest_play";
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));  // IDC_ARROW (非UNICODE构建无W宏)
    RegisterClassW(&wc);
    HWND hwnd = CreateWindowW(wc.lpszClassName, L"avoxtest play matrix",
                              WS_OVERLAPPEDWINDOW | WS_VISIBLE, 60, 60, 960, 540,
                              nullptr, nullptr, wc.hInstance, nullptr);
    if (!hwnd) {
      std::printf("[warn] create window failed, fallback offscreen\n");
      winMode = false;
    } else {
      int code = 0;
      std::thread runner([&] {
        code = runAll(cases, (void*)hwnd, opt);
        // 矩阵跑完投递关闭: WM_CLOSE→DestroyWindow→WM_DESTROY→PostQuitMessage
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
      });
      MSG msg;
      while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
      }
      runner.join();
      DestroyWindow(hwnd);
      AvoxManager::clean();
      return code;
    }
  }
#endif
  (void)winMode;
  int code = runAll(cases, nullptr, opt);
  // Android console 没有 DllMain(DETACH) 兜底, 退出前显式有序清理
  // (mk_env_release 等 cleanFuncs), 否则 libmk_api 静态析构序倒挂退出必崩
  AvoxManager::clean();
  return code;
}
