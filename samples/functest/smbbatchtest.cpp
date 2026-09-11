// smbbatchtest: SMB 共享媒体播放普查 — smbsourcetest 同一通路的批量版。
// 遍历 share(递归) 收集全部媒体条目并就地 resolve, 逐个:
//   open → onReady → 稳态推进(playSec) → (时长≥120s 时) seek 到中点
// 逐文件打一行结果, 末尾汇总; 全部通过退出码 0。
// 判定: fail-open(onReady超时/IO错) / fail-advance(位置不推进或播放中IO错);
//       seek 结果单列不计入文件判定(网络源 seek 慢属服务端/文件特性)。
//
// 用法:
//   smbbatchtest -u smb://host[:port]/share[/dir] [-n user] [-p pass]
//                [--max N] [--stride N] [--min-size MB] [--open-timeout ms]
//                [--play-sec N] [--no-seek] [--depth N] [--name 子串过滤] [-v]
// 示例:
//   smbbatchtest -u smb://192.168.3.20/sata1-139XXXX2391 -n user -p pass
//   smbbatchtest -u smb://192.168.3.20/share -p "" --max 10 --no-seek

#include <algorithm>
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

// open/list 完成信号(与 smbsourcetest 同款, 先 arm 再发起请求)
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

// 播放回调(单 player 复用, 每个文件前 reset)
struct PlayerOb : IMediaPlayerOb {
  std::mutex mtx;
  std::condition_variable cv;
  bool ready = false;
  std::atomic<bool> playing{false};
  std::atomic<bool> seekSeen{false};
  std::atomic<bool> hasError{false};
  std::string errorMsg;

  void reset() {
    std::lock_guard<std::mutex> lk(mtx);
    ready = false;
    playing.store(false);
    seekSeen.store(false);
    hasError.store(false);
    errorMsg.clear();
  }
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
  bool waitReady(int32_t timeoutMs) {
    std::unique_lock<std::mutex> lk(mtx);
    return cv.wait_for(lk, std::chrono::milliseconds(timeoutMs),
                       [&] { return ready; }) &&
           ready;
  }
};

struct Candidate {
  std::string url;
  std::string name;
  std::string token;
  uint64_t size = 0;
};

std::string humanSize(uint64_t bytes) {
  char buf[32];
  if (bytes >= 1ull << 30) {
    snprintf(buf, sizeof(buf), "%.2fGB", (double)bytes / (1ull << 30));
  } else if (bytes >= 1ull << 20) {
    snprintf(buf, sizeof(buf), "%.1fMB", (double)bytes / (1ull << 20));
  } else {
    snprintf(buf, sizeof(buf), "%lluB", (unsigned long long)bytes);
  }
  return buf;
}

std::string humanTime(int64_t ms) {
  if (ms <= 0) {
    return "?";
  }
  int64_t s = ms / 1000;
  char buf[32];
  snprintf(buf, sizeof(buf), "%lld:%02lld", (long long)(s / 60),
           (long long)(s % 60));
  return buf;
}

std::string clipName(const std::string& s, size_t width = 64) {
  // 按字节截断但避开 UTF-8 多字节中间(尾字节 10xxxxxx)
  if (s.size() <= width) {
    return s;
  }
  size_t cut = width;
  while (cut > 0 && ((unsigned char)s[cut] & 0xC0) == 0x80) {
    --cut;
  }
  return s.substr(0, cut) + "...";
}

}  // namespace

int runMain(int argc, char** argv) {
  std::string url;
  std::string user;
  std::string pass;
  std::string nameFilter;
  int maxFiles = 0;        // 0=全部
  int stride = 1;          // 每隔N个候选测1个(0/1=全测), 用于大共享等距抽样
  int minSizeMb = 1;       // 小于此尺寸视为非视频残片, 跳过
  int openTimeoutMs = 20000;
  int playSec = 3;
  int depth = 6;
  bool doSeek = true;
  bool verbose = false;
  for (int i = 1; i < argc; ++i) {
    auto next = [&]() -> const char* {
      return argv[i + 1] != nullptr && i + 1 < argc ? argv[++i] : "";
    };
    if (std::strcmp(argv[i], "-u") == 0) {
      url = next();
    } else if (std::strcmp(argv[i], "-n") == 0) {
      user = next();
    } else if (std::strcmp(argv[i], "-p") == 0) {
      pass = next();
    } else if (std::strcmp(argv[i], "--name") == 0) {
      nameFilter = next();
    } else if (std::strcmp(argv[i], "--max") == 0) {
      maxFiles = std::atoi(next());
    } else if (std::strcmp(argv[i], "--stride") == 0) {
      stride = std::atoi(next());
    } else if (std::strcmp(argv[i], "--min-size") == 0) {
      minSizeMb = std::atoi(next());
    } else if (std::strcmp(argv[i], "--open-timeout") == 0) {
      openTimeoutMs = std::atoi(next());
    } else if (std::strcmp(argv[i], "--play-sec") == 0) {
      playSec = std::atoi(next());
    } else if (std::strcmp(argv[i], "--depth") == 0) {
      depth = std::atoi(next());
    } else if (std::strcmp(argv[i], "--no-seek") == 0) {
      doSeek = false;
    } else if (std::strcmp(argv[i], "-v") == 0) {
      verbose = true;
    }
  }
  if (url.empty()) {
    printf("用法: smbbatchtest -u smb://host[:port]/share[/dir] [-n user] [-p pass]\n"
           "  [--max N] [--min-size MB] [--open-timeout ms] [--play-sec N]\n"
           "  [--no-seek] [--depth N] [--name 子串] [-v]\n");
    return 2;
  }
  // 引擎日志只放行 warn/error, 否则逐包 info 会淹没结果行
  setLogAction([](int32_t level, const char* message) {
    if (level == 1 || level == 2) {
      fprintf(stdout, "%s\n", message != nullptr ? message : "");
      fflush(stdout);
    }
  });
  printf("smbbatchtest rev 2026-09-11.2\n");

  // ---- 会话 ----
  IRemoteSource* src = createRemoteSource("smb");
  if (src == nullptr) {
    printf("RESULT create fail createRemoteSource(smb) 返回 nullptr\n");
    return 1;
  }
  SessionOb ob;
  src->setOb(&ob);
  ob.arm();
  if (!src->open(url.c_str(), user.c_str(), pass.c_str(), nullptr, 10000)) {
    printf("RESULT open fail: %s\n", src->getLastError());
    return 1;
  }
  if (!ob.waitOpen(15000)) {
    printf("RESULT open fail: %s\n", src->getLastError());
    return 1;
  }
  printf("open ok: %s\n", src->getSessionField("name"));
  fflush(stdout);

  // ---- 全量收集: 递归遍历, 批次内就地 resolve(批次是活状态, 必须快照) ----
  std::vector<Candidate> cands;
  int listErr = 0;
  int walkedDirs = 0;
  long long candSeen = 0;  // 全量媒体计数(stride 抽样基准)
  std::function<bool(const std::string&, int)> walk =
      [&](const std::string& token, int dep) -> bool {
    if (dep > depth) {
      return true;
    }
    if (++walkedDirs % 20 == 0) {
      printf("...walk %d dirs, 候选 %zu, 当前 %s\n", walkedDirs, cands.size(),
             clipName(token, 70).c_str());
      fflush(stdout);
    }
    ob.arm();
    if (!src->list(token.c_str(), 10000) || !ob.waitList(15000)) {
      ++listErr;
      if (verbose) {
        printf("TRACE list(%s) 失败: %s\n", token.c_str(),
               src->getLastError());
        fflush(stdout);
      }
      return true;  // 单目录失败不阻断整体
    }
    int32_t n = src->getEntryCount();
    if (verbose) {
      printf("TRACE list(%s) -> %d 项\n", token.c_str(), n);
      fflush(stdout);
    }
    // 先快照(目录条目数恒定), media 随后在批次存活期内 resolve
    std::vector<std::pair<int32_t, std::string>> mediaIdx;  // idx, token
    std::vector<std::string> dirs;
    for (int32_t i = 0; i < n; ++i) {
      RemoteEntryType t = src->getEntryType(i);
      if (t == RemoteEntryType::media) {
        mediaIdx.emplace_back(i, src->getEntryToken(i));
      } else if (t == RemoteEntryType::dir) {
        std::string dn = src->getEntryName(i);
        // 点目录是服务端私有空间/下载任务元数据, 默认跳过
        if (!dn.empty() && dn[0] != '.') {
          dirs.push_back(src->getEntryToken(i));
        }
      }
    }
    for (auto& mi : mediaIdx) {
      Candidate c;
      c.token = mi.second;
      c.name = c.token.substr(c.token.find_last_of('/') + 1);
      c.size = src->getEntrySize(mi.first);
      if (!nameFilter.empty() && c.name.find(nameFilter) == std::string::npos) {
        continue;
      }
      ++candSeen;
      // 等距抽样: 大共享全测耗时线性爆炸, 每隔 stride 个取 1 个均匀覆盖
      if (stride > 1 && (candSeen - 1) % stride != 0) {
        continue;
      }
      if (maxFiles > 0 && (int)cands.size() >= maxFiles) {
        return true;
      }
      const char* u = src->resolve(mi.first, nullptr);
      if (u == nullptr) {
        ++listErr;
        continue;
      }
      c.url = u;  // resolve 返回内部缓冲, 立即拷贝
      cands.push_back(std::move(c));
    }
    for (auto& d : dirs) {
      if (maxFiles > 0 && (int)cands.size() >= maxFiles) {
        return true;
      }
      walk(d, dep + 1);
    }
    return true;
  };
  walk("/", 0);
  printf("全盘媒体 %lld 个, 抽样测试 %zu 个(stride=%d, 目录失败 %d)\n",
         candSeen, cands.size(), stride, listErr);
  fflush(stdout);

  // ---- 逐个播放 ----
  IMediaPlayer* mp = createMediaPlayer();
  PlayerOb pob;
  addMediaPlayerOb(mp, &pob);
  int okCnt = 0, failOpen = 0, failAdvance = 0, skipTiny = 0;
  int seekOk = 0, seekFail = 0, seekSkip = 0;
  std::vector<std::string> failNames;
  auto tAll = Clock::now();
  for (size_t i = 0; i < cands.size(); ++i) {
    const Candidate& c = cands[i];
    printf("[%zu/%zu] %s (%s) ", i + 1, cands.size(), clipName(c.name).c_str(),
           humanSize(c.size).c_str());
    fflush(stdout);
    if ((int64_t)c.size < (int64_t)minSizeMb * 1024 * 1024) {
      ++skipTiny;
      printf("skip-tiny(非视频残片?)\n");
      fflush(stdout);
      continue;
    }
    pob.reset();
    mp->open(c.url.c_str());
    auto t0 = Clock::now();
    bool ready = pob.waitReady(openTimeoutMs);
    int64_t readyMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t0)
            .count();
    if (!ready || pob.hasError.load()) {
      ++failOpen;
      ++g_fail;
      std::string err = pob.errorMsg.empty() ? "onReady超时" : pob.errorMsg;
      printf("FAIL-open readyMs=%lld err=%s\n", (long long)readyMs,
             clipName(err, 80).c_str());
      failNames.push_back(c.name + " (open: " + err + ")");
      fflush(stdout);
      mp->close();
      continue;
    }
    int64_t dur = mp->getDuration();
    // 等 playing + 1s 过渡: 首帧渲染前 getPosition 退回 demux 位置(设计如此)
    auto tp = Clock::now();
    while (!pob.playing.load() &&
           Clock::now() - tp < std::chrono::seconds(8)) {
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    std::this_thread::sleep_for(std::chrono::seconds(1));
    int64_t pos0 = mp->getPosition();
    // 推进窗口: 轮询播放位置与完成状态 — 短文件/残片(声明时长>实际数据)
    // 可能在窗口内播完触发 completed, 播完也算通过
    bool completed = false;
    for (int t = 0; t < (playSec > 0 ? playSec : 1) * 10; ++t) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      if (mp->getState() == PlayerState::completed) {
        completed = true;
        break;
      }
    }
    int64_t pos1 = mp->getPosition();
    int dbgState = (int)mp->getState();  // completed=8; 调试残留观察用
    bool advanced = (completed || pos1 > pos0) && !pob.hasError.load();
    if (!advanced) {
      ++failAdvance;
      ++g_fail;
      std::string err = pob.errorMsg.empty() ? "位置不推进" : pob.errorMsg;
      printf("FAIL-advance dur=%s readyMs=%lld pos %lld -> %lld state=%d err=%s\n",
             humanTime(dur).c_str(), (long long)readyMs, (long long)pos0,
             (long long)pos1, dbgState, clipName(err, 80).c_str());
      failNames.push_back(c.name + " (advance: " + err + ")");
      fflush(stdout);
      mp->close();
      continue;
    }
    ++okCnt;
    ++g_pass;
    printf("ok%s dur=%s readyMs=%lld adv+%lldms", completed ? "(播完)" : "",
           humanTime(dur).c_str(), (long long)readyMs, (long long)(pos1 - pos0));
    // seek: 长文件才做, 结果单列
    if (!doSeek || completed || dur < 120000) {
      ++seekSkip;
    } else {
      pob.seekSeen.store(false);
      mp->seek(dur / 2);
      std::this_thread::sleep_for(std::chrono::seconds(3));
      bool sok = mp->getPosition() > dur / 4;
      if (sok) {
        ++seekOk;
        printf(" seek=ok");
      } else {
        ++seekFail;
        printf(" seek=stuck");
      }
    }
    printf("\n");
    fflush(stdout);
    mp->close();
  }
  auto totalMs =
      std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - tAll)
          .count();
  removeMediaPlayerOb(mp, &pob);
  mp->close();
  src->close();
  delete src;  // create* 约定: 虚析构释放

  printf("\n==== 汇总 ====\n");
  printf("候选 %zu | 通过 %d | fail-open %d | fail-advance %d | 跳过小文件 %d\n",
         cands.size(), okCnt, failOpen, failAdvance, skipTiny);
  printf("seek: ok %d / stuck %d / 未做 %d | 总耗时 %llds\n", seekOk, seekFail,
         seekSkip, (long long)(totalMs / 1000));
  for (auto& f : failNames) {
    printf("  [FAIL] %s\n", clipName(f, 100).c_str());
  }
  printf("SUMMARY %d/%d\n", g_pass, g_pass + g_fail);
  return g_fail == 0 ? 0 : 1;
}

// Windows 下 wmain + UTF-8 转码(中文共享名/过滤串经命令行传入)
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
