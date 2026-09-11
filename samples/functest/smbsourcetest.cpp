// smbsourcetest: avox_remote "smb" 源链路冒烟(黑盒, 走公共API)。
// 阶段: open会话 → list根 → 下钻目录(如有) → resolve媒体 → 播放(onReady/推进) → seek。
// 每阶段向 stdout 打 "RESULT <stage> <pass|fail> <note>" 行, 全部通过退出码0。
//
// 用法:
//   smbsourcetest -u smb://host[:port]/share[/dir] [-n user] [-p pass]
//                 [-m <media名子串>] [--play-sec 6] [--no-play]
// 示例:
//   smbsourcetest -u smb://192.168.1.10/media -n guest -p ""
//   smbsourcetest -u "smb://DESKTOP\\media" -m big_buck_bunny

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "avox/Avox.hpp"
#include "avox/AvoxBase.h"
#include "avox/AvoxLog.h"
#include "avox/AvoxPlayer.h"

#ifdef _WIN32
#include <windows.h>
#endif

using namespace avox;

namespace {

using Clock = std::chrono::steady_clock;

int g_pass = 0;
int g_fail = 0;

void result(const char* stage, bool ok, const std::string& note = "") {
  printf("RESULT %s %s %s\n", stage, ok ? "pass" : "fail", note.c_str());
  fflush(stdout);
  if (ok) {
    ++g_pass;
  } else {
    ++g_fail;
  }
}

// open/list 完成信号
struct SessionOb : IRemoteSourceOb {
  std::mutex mtx;
  std::condition_variable cv;
  int32_t openCode = 1;
  int32_t listCode = 1;
  bool openDone = false;
  bool listDone = false;

  void onOpenResult(int32_t code) override {
    std::lock_guard<std::mutex> lk(mtx);
    openCode = code;
    openDone = true;
    cv.notify_all();
  }
  void onListResult(int32_t code) override {
    std::lock_guard<std::mutex> lk(mtx);
    listCode = code;
    listDone = true;
    cv.notify_all();
  }
  // 必须在下一次 list()/open() 发起前调用, 否则会等到上一批的陈旧完成标志
  void arm() {
    std::lock_guard<std::mutex> lk(mtx);
    openDone = false;
    listDone = false;
  }
  bool waitOpen(int32_t timeoutMs) {
    std::unique_lock<std::mutex> lk(mtx);
    return cv.wait_for(lk, std::chrono::milliseconds(timeoutMs),
                       [&] { return openDone; }) &&
           openCode == 0;
  }
  bool waitList(int32_t timeoutMs) {
    std::unique_lock<std::mutex> lk(mtx);
    return cv.wait_for(lk, std::chrono::milliseconds(timeoutMs),
                       [&] { return listDone; }) &&
           listCode == 0;
  }
};

// 播放回调: onReady/推进/错误
struct PlayerOb : IMediaPlayerOb {
  std::mutex mtx;
  std::condition_variable cv;
  bool ready = false;
  std::atomic<bool> playing{false};
  std::atomic<bool> seekSeen{false};
  std::atomic<bool> hasError{false};
  std::string errorMsg;

  void onReady() override {
    std::lock_guard<std::mutex> lk(mtx);
    ready = true;
    cv.notify_all();
  }
  void onStateChange(PlayerState pre, PlayerState state) override {
    if (state == PlayerState::playing) {
      playing.store(true);
    }
  }
  void onSeek() override { seekSeen.store(true); }
  void onIoError(AVError error, const char* msg) override {
    hasError.store(true);
    errorMsg = msg != nullptr ? msg : "";
  }
};

}  // namespace

int runMain(int argc, char** argv) {
  std::string url;
  std::string user;
  std::string pass;
  std::string mediaHint;
  int playSec = 6;
  bool doPlay = true;
  bool verbose = false;
  for (int i = 1; i < argc; ++i) {
    auto next = [&]() -> const char* { return argv[i + 1] != nullptr && i + 1 < argc ? argv[++i] : ""; };
    if (std::strcmp(argv[i], "-u") == 0) {
      url = next();
    } else if (std::strcmp(argv[i], "-n") == 0) {
      user = next();
    } else if (std::strcmp(argv[i], "-p") == 0) {
      pass = next();
    } else if (std::strcmp(argv[i], "-m") == 0) {
      mediaHint = next();
    } else if (std::strcmp(argv[i], "--play-sec") == 0) {
      playSec = std::atoi(next());
    } else if (std::strcmp(argv[i], "--no-play") == 0) {
      doPlay = false;
    } else if (std::strcmp(argv[i], "-v") == 0) {
      verbose = true;
    }
  }
  if (url.empty()) {
    printf("用法: smbsourcetest -u smb://host[:port]/share[/dir] [-n user] [-p pass]"
           " [-m 媒体名子串] [--play-sec N] [--no-play]\n");
    return 2;
  }

  // ---- 阶段1: 会话 ----
  IRemoteSource* src = createRemoteSource("smb");
  if (src == nullptr) {
    result("create", false, "createRemoteSource(smb) 返回 nullptr(插件未装/未编)");
    return 1;
  }
  result("create", true);
  SessionOb ob;
  src->setOb(&ob);
  if (!src->open(url.c_str(), user.c_str(), pass.c_str(), nullptr, 10000)) {
    result("open", false, "open 返回 false: " + std::string(src->getLastError()));
    return 1;
  }
  bool openOk = ob.waitOpen(15000);
  result("open", openOk, openOk ? src->getSessionField("name")
                                : std::string(src->getLastError()));
  if (!openOk) {
    return 1;
  }

  // ---- 阶段2: 列根 + 递归找一个可播媒体 ----
  std::string mediaToken;
  std::string mediaName;
  std::function<bool(const std::string&, int)> walk = [&](const std::string& token,
                                                          int depth) -> bool {
    if (depth > 3 || !mediaToken.empty()) {
      return !mediaToken.empty();
    }
    ob.arm();
    if (!src->list(token.c_str(), 10000)) {
      if (verbose) {
        printf("TRACE d%d list(%s) 拒绝: running/未open\n", depth, token.c_str());
        fflush(stdout);
      }
      return false;
    }
    if (!ob.waitList(15000)) {
      if (verbose) {
        printf("TRACE d%d list(%s) 超时/错: %s\n", depth, token.c_str(),
               src->getLastError());
        fflush(stdout);
      }
      return false;
    }
    int32_t n = src->getEntryCount();
    if (verbose) {
      printf("TRACE d%d list(%s) -> %d 项\n", depth, token.c_str(), n);
      fflush(stdout);
    }
    // 批次是会话内的活状态(下次 list 整体替换): 先快照媒体/子目录 token 再下钻
    std::vector<std::string> medias, dirs;
    for (int32_t i = 0; i < n; ++i) {
      RemoteEntryType t = src->getEntryType(i);
      if (t == RemoteEntryType::media) {
        medias.push_back(src->getEntryToken(i));
      } else if (t == RemoteEntryType::dir) {
        dirs.push_back(src->getEntryToken(i));
      }
    }
    for (const auto& m : medias) {
      std::string name = m.substr(m.find_last_of('/') + 1);
      if (mediaHint.empty() || name.find(mediaHint) != std::string::npos) {
        mediaToken = m;
        mediaName = name;
        return true;
      }
    }
    for (const auto& d : dirs) {
      if (walk(d, depth + 1)) {
        return true;
      }
    }
    return !mediaToken.empty();
  };
  bool found = walk("/", 0);
  result("list", found,
         found ? mediaToken + " (" + std::to_string(src->getEntrySize(
                                       src->getEntryCount() - 1)) +
                       "B 末项)"
               : "未找到可播媒体(挂共享里放个 .mp4/.mkv 再试)");
  if (!found) {
    return 1;
  }

  // ---- 阶段3: resolve ----
  // walk 结束后的批次已不在 media 所在目录, 重新列一次父目录再 resolve
  std::string dirOf = mediaToken;
  size_t slash = dirOf.find_last_of('/');
  dirOf = slash == std::string::npos ? "/" : dirOf.substr(0, slash + 1);
  ob.arm();
  src->list(dirOf.c_str(), 10000);
  ob.waitList(15000);
  const char* play = nullptr;
  for (int32_t i = 0; i < src->getEntryCount(); ++i) {
    if (mediaName == src->getEntryName(i)) {
      play = src->resolve(i, nullptr);
      break;
    }
  }
  result("resolve", play != nullptr, play != nullptr ? play : "resolve 返回 nullptr");
  if (play == nullptr || !doPlay) {
    printf("SUMMARY %d/%d\n", g_pass, g_pass + g_fail);
    return g_fail == 0 ? 0 : 1;
  }

  // ---- 阶段4: 播放(resolve 的 smb:// 链接走 IoPlan::smb 自动路由) ----
  IMediaPlayer* mp = createMediaPlayer();
  PlayerOb pob;
  addMediaPlayerOb(mp, &pob);
  mp->open(play);
  bool ready = false;
  auto t0 = Clock::now();
  while (Clock::now() - t0 < std::chrono::seconds(20)) {
    {
      std::lock_guard<std::mutex> lk(pob.mtx);
      ready = pob.ready;
    }
    if (ready || pob.hasError.load()) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  result("play-open", ready && !pob.hasError.load(),
         ready ? "onReady" : ("超时/错误: " + pob.errorMsg));

  // 稳态播放 playSec 秒, 看进度推进(open 即自动播放)。
  // 注意: 首帧渲染前 getPosition() 退回 demux 位置(MediaPlayer 设计如此),
  // 本地快源该值会瞬时冲高; 等 playing 且过渡 1s, pos0 才是渲染时钟值
  bool advanced = false;
  if (ready) {
    auto tPlay = Clock::now();
    while (!pob.playing.load() &&
           Clock::now() - tPlay < std::chrono::seconds(5)) {
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    std::this_thread::sleep_for(std::chrono::seconds(1));
    int64_t pos0 = mp->getPosition();
    std::this_thread::sleep_for(std::chrono::seconds(playSec > 0 ? playSec : 1));
    int64_t pos1 = mp->getPosition();
    advanced = pos1 > pos0;
    result("play-advance", advanced,
           "pos " + std::to_string(pos0) + " -> " + std::to_string(pos1) + "ms");
    // seek 到中点, 等播放状态恢复
    int64_t dur = mp->getDuration();
    if (dur > 20000) {
      pob.seekSeen.store(false);
      mp->seek(dur / 2);
      result("seek", pob.seekSeen.load() || mp->getPosition() > dur / 4,
             "seek to " + std::to_string(dur / 2) + "ms");
      std::this_thread::sleep_for(std::chrono::seconds(2));
      result("seek-advance", mp->getPosition() > dur / 4,
             "pos " + std::to_string(mp->getPosition()) + "ms");
    }
  }

  mp->close();
  removeMediaPlayerOb(mp, &pob);
  src->close();
  delete src;  // create* 约定: 虚析构释放
  printf("SUMMARY %d/%d\n", g_pass, g_pass + g_fail);
  return g_fail == 0 ? 0 : 1;
}

// Windows 下 wmain + UTF-8 转码: 中文共享名/媒体名经命令行传入时,
// ANSI main 会拿到 GBK 字节直发服务端导致 NOT_FOUND
#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
  std::vector<std::string> u8;
  for (int i = 0; i < argc; ++i) {
    int len = WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, nullptr, 0,
                                  nullptr, nullptr);
    u8.emplace_back(len > 0 ? (size_t)len - 1 : 0, '\0');
    if (len > 1) {
      WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, u8.back().data(), len,
                          nullptr, nullptr);
    }
  }
  std::vector<char*> uv;
  for (auto& s : u8) {
    uv.push_back(s.data());
  }
  return runMain((int)uv.size(), uv.data());
}
#else
int main(int argc, char** argv) { return runMain(argc, argv); }
#endif
