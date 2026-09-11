// torrentbench: 磁力/torrent 播放链路分步计时基准(黑盒, 走公共API)
// 阶段: 探测(文件列表) → 起播(onReady/playing) → 稳态消费(追帧) → 多处seek(恢复)
// 每阶段向 stdout 打 "RESULT <tag> <stage> <value> <note>" 行, 由
// script/torrent/torrent_bench.py 汇总成表并跨优化版本对比。
//
// 用法:
//   torrentbench -i <magnet|torrent路径> [--tag X] [-o result.csv]
//                [--probe-timeout 45000] [--play-sec 12] [--seeks 25,50,75]
//                [--file-index -1] [--cache-dir DIR] [--fresh]
//                [--no-probe] [--probe-only]
//   torrentbench --list urls.txt [其他选项同上]   # 单进程顺序跑多条(热会话)

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <sstream>
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

int64_t millisSince(Clock::time_point t0) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t0)
      .count();
}

// 会话回调: open/list 完成后置位并唤醒 (open=元数据探测, list=文件树枚举)
struct SessionOb : IRemoteSourceOb {
  std::mutex mtx;
  std::condition_variable cv;
  bool openDone = false;
  bool listDone = false;
  int32_t openCode = -1;
  int32_t listCode = -1;
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
  bool waitOpen(int64_t timeoutMs) {
    std::unique_lock<std::mutex> lk(mtx);
    return cv.wait_for(lk, std::chrono::milliseconds(timeoutMs),
                       [&] { return openDone; });
  }
  bool waitList(int64_t timeoutMs) {
    std::unique_lock<std::mutex> lk(mtx);
    return cv.wait_for(lk, std::chrono::milliseconds(timeoutMs),
                       [&] { return listDone; });
  }
};

// 播放器观察者: 记录 ready/playing 时刻与状态流转, 每次状态变化唤醒等待方
struct PlayerOb : IMediaPlayerOb {
  std::mutex mtx;
  std::condition_variable cv;
  std::atomic<bool> ready{false};
  std::atomic<bool> playing{false};
  Clock::time_point readyAt{};
  Clock::time_point playingAt{};
  std::string ioErrMsg = "";
  std::atomic<bool> ioError{false};
  void onReady() override {
    std::lock_guard<std::mutex> lk(mtx);
    if (!ready) readyAt = Clock::now();
    ready = true;
    cv.notify_all();
  }
  void onStateChange(PlayerState, PlayerState state) override {
    std::lock_guard<std::mutex> lk(mtx);
    if (state == PlayerState::playing && !playing) playingAt = Clock::now();
    if (state == PlayerState::playing) playing = true;
    else playing = false;
    cv.notify_all();
  }
  void onIoError(AVError, const char* msg) override {
    std::lock_guard<std::mutex> lk(mtx);
    ioError = true;
    if (msg) ioErrMsg = msg;
    cv.notify_all();
  }
  bool waitFor(std::function<bool()> pred, int64_t timeoutMs) {
    std::unique_lock<std::mutex> lk(mtx);
    return cv.wait_for(lk, std::chrono::milliseconds(timeoutMs), pred);
  }
};

// 进程起点(与LogFileOb的t0同源), MARK打点供驱动脚本对齐引擎日志窗口
static Clock::time_point g_tProc;

// 结果输出: stdout RESULT/MARK行 + 可选CSV追加
struct ResultSink {
  std::string tag;
  std::ofstream csv;
  void open(const std::string& path) {
    if (!path.empty()) {
      csv.open(path, std::ios::app);
    }
  }
  void row(const std::string& stage, int64_t valueMs,
           const std::string& note = "") {
    printf("RESULT %s %s %lld %s\n", tag.c_str(), stage.c_str(),
           (long long)valueMs, note.c_str());
    fflush(stdout);
    if (csv.is_open()) {
      csv << tag << "," << stage << "," << valueMs << "," << note << "\n";
      csv.flush();
    }
  }
  // 阶段边界打点(ms, 进程起点基准): 脚本据此截取引擎日志算窗口内速率
  void mark(const std::string& name) {
    int64_t ms = millisSince(g_tProc);
    printf("MARK %s %lld\n", name.c_str(), (long long)ms);
    fflush(stdout);
    if (csv.is_open()) {
      csv << tag << ",mark_" << name << "," << ms << ",\n";
    }
  }
};

// 引擎/播放器日志落盘(诊断 open_ready 等阶段耗时的构成); 时间基准与MARK一致。
// avox多线程回调本接口, ofstream非线程安全, 必须加锁
struct LogFileOb : ILogOb {
  std::ofstream f;
  std::mutex mtx;
  bool open(const std::string& path) {
    f.open(path, std::ios::app);
    return f.is_open();
  }
  void onLogEvent(int level, const char* message) override {
    if (!f.is_open() || !message) return;
    std::lock_guard<std::mutex> lk(mtx);
    f << "[" << millisSince(g_tProc) << "ms] L" << level << " " << message
      << "\n";
    f.flush();
  }
};

// 解析 25,50,75 形式的seek百分比列表
std::vector<int32_t> parseSeeks(const std::string& src) {
  std::vector<int32_t> out;
  std::string cur;
  for (char c : src) {
    if (c == ',') {
      if (!cur.empty()) out.push_back(atoi(cur.c_str()));
      cur.clear();
    } else {
      cur.push_back(c);
    }
  }
  if (!cur.empty()) out.push_back(atoi(cur.c_str()));
  return out;
}

// 单条URL全流程计时
void runOne(const std::string& tag, const std::string& url,
            const std::string& cacheDir, int32_t probeTimeoutMs,
            int32_t playSec, const std::vector<int32_t>& seeks,
            int32_t fileIndex, bool doProbe, bool probeOnly, ResultSink& sink) {
  // ---- 阶段1: 会话建立(元数据探测) + 根列表 ----
  if (doProbe) {
    std::unique_ptr<IRemoteSource> src(createRemoteSource("torrent"));
    if (!src) {
      sink.row("error", -1, "createRemoteSource nullptr(插件未加载?)");
      return;
    }
    SessionOb sob;
    src->setOb(&sob);
    if (!cacheDir.empty()) {
      src->setParam("cacheDir", cacheDir.c_str());
    }
    sink.mark("probe_begin");
    auto t0 = Clock::now();
    bool started =
        src->open(url.c_str(), nullptr, nullptr, nullptr, probeTimeoutMs);
    if (!started) {
      sink.row("error", -1, "open false(有操作在进行或参数非法)");
      return;
    }
    // open = 元数据探测(慢, 有界); list(根) = 文件树即时枚举
    bool openOk = sob.waitOpen((int64_t)probeTimeoutMs * 2 + 10000);
    if (!openOk || sob.openCode != 0) {
      std::string note =
          openOk ? ("open code=" + std::to_string(sob.openCode) + " " +
                    src->getLastError())
                 : "open wait timeout";
      sink.mark("probe_end");
      sink.row("probe_fail", millisSince(t0), note);
      if (probeOnly) return;
    } else if (!src->list("", 0) ||
               !sob.waitList((int64_t)probeTimeoutMs * 2 + 10000) ||
               sob.listCode != 0) {
      sink.mark("probe_end");
      sink.row("probe_fail", millisSince(t0), "root list failed");
      if (probeOnly) return;
    } else {
      sink.mark("probe_end");
      sink.row("probe_ms", millisSince(t0),
               "files=" + std::to_string(src->getEntryCount()) + " name=" +
                   src->getSessionField("name"));
      if (probeOnly) return;
    }
    src->close();
  }
  // ---- 阶段2: 起播(engine start + avformat open) ----
  IMediaPlayer* mp = createMediaPlayer();
  if (!mp) {
    sink.row("error", -1, "createMediaPlayer nullptr");
    return;
  }
  mp->setHardDecode(false);
  mp->setIoPlan(IoPlan::torrent);
  auto* opt = mp->getOption();
  opt->setInt("io.timeout.ms", 20000);
  if (!cacheDir.empty()) opt->setString("torrent.cacheDir", cacheDir.c_str());
  if (fileIndex >= 0) opt->setInt("torrent.fileIndex", fileIndex);
  // 离屏渲染: 无窗口跑通解码渲染管线
  mp->getSurfaceRender()->setOffSurface(YuvType::yuv420P);
  PlayerOb pob;
  addMediaPlayerOb(mp, &pob);
  auto tOpen = Clock::now();
  sink.mark("open_begin");
  mp->open(url.c_str());
  // onReady: 轨道信息就绪(avformat open完成); io错误(如编码不支持)立即退出
  bool readyOk = pob.waitFor([&pob] { return pob.ready.load() || pob.ioError.load(); },
                             (int64_t)probeTimeoutMs * 2 + 60000);
  sink.mark("open_end");
  if (!readyOk || pob.ioError.load()) {
    sink.row("open_ready_fail", millisSince(tOpen),
             pob.ioError.load() ? "io:" + pob.ioErrMsg : "wait timeout");
    removeMediaPlayerOb(mp, &pob);
    mp->close();
    return;
  }
  sink.row("open_ready_ms", millisSince(tOpen));
  int64_t durationMs = mp->getDuration();
  // playing: 首帧出画进入播放态
  bool playOk = pob.waitFor([&pob] { return pob.playing.load(); }, 60000);
  sink.row(playOk ? "playing_ms" : "playing_fail",
           playOk ? millisSince(tOpen) : -1,
           "duration_ms=" + std::to_string(durationMs));
  if (!playOk) {
    removeMediaPlayerOb(mp, &pob);
    mp->close();
    return;
  }
  // ---- 阶段3: 稳态消费(追帧能力: 消费playSec内容实际花 wall 多久) ----
  if (playSec > 0) {
    auto tSteady = Clock::now();
    sink.mark("steady_begin");
    int64_t startPos = mp->getPosition();
    int64_t wall = 0;
    while (true) {
      std::this_thread::sleep_for(std::chrono::milliseconds(200));
      wall = millisSince(tSteady);
      if (mp->getPosition() - startPos >= (int64_t)playSec * 1000) break;
      if (pob.ioError || mp->getState() == PlayerState::completed) break;
      if (wall > (int64_t)playSec * 4000 + 30000) break;
    }
    int64_t advanced = mp->getPosition() - startPos;
    sink.mark("steady_end");
    sink.row("steady_ms", wall,
             "adv_ms=" + std::to_string(advanced) + " speed_ratio=" +
                 std::to_string((double)advanced / (double)std::max<int64_t>(wall, 1)));
  }
  // ---- 阶段4: 多处seek(用户体感: seek到恢复出画) ----
  for (int32_t pct : seeks) {
    if (durationMs <= 30000) break;  // 太短的素材seek无意义
    int64_t target = durationMs * pct / 100;
    std::this_thread::sleep_for(std::chrono::seconds(3));  // 让上一动作稳定
    auto tSeek = Clock::now();
    sink.mark("seek" + std::to_string(pct) + "_begin");
    mp->seek(target);
    int64_t latency = -1;
    int64_t nearPos = -1;
    // 二段判据: 先到目标位置域(demux seek完成), 再确认位置继续前进800ms
    // (帧真实出画, 数据已供上) —— 防"position先跳到目标但数据未就绪"的假快
    while (true) {
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
      int64_t wall = millisSince(tSeek);
      int64_t pos = mp->getPosition();
      bool nearTarget = pos >= target - 6000 && pos <= target + 15000;
      if (nearTarget && nearPos < 0) nearPos = pos;
      if (nearPos >= 0 && pos - nearPos >= 800 &&
          mp->getState() == PlayerState::playing) {
        latency = wall;
        break;
      }
      if (wall > 120000) break;  // 2min兜底
    }
    char buf[32];
    snprintf(buf, sizeof(buf), "seek%d_ms", pct);
    sink.mark("seek" + std::to_string(pct) + "_end");
    sink.row(buf, latency, "target_ms=" + std::to_string(target));
  }
  removeMediaPlayerOb(mp, &pob);
  mp->close();
}
}  // namespace

int main(int argc, char* argv[]) {
  g_tProc = Clock::now();
  std::string url, listFile, tag = "case", outFile, cacheDir, seeksStr = "25,50,75";
  int32_t probeTimeoutMs = 45000;
  int32_t playSec = 12;
  int32_t fileIndex = -1;
  bool fresh = false, doProbe = true, probeOnly = false;
  std::vector<std::string> urls;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    auto next = [&](void) -> const char* {
      return (i + 1 < argc) ? argv[++i] : "";
    };
    if (a == "-i" || a == "--input") url = next();
    else if (a == "--list") listFile = next();
    else if (a == "--tag") tag = next();
    else if (a == "-o" || a == "--out") outFile = next();
    else if (a == "--probe-timeout") probeTimeoutMs = atoi(next());
    else if (a == "--play-sec") playSec = atoi(next());
    else if (a == "--seeks") seeksStr = next();
    else if (a == "--file-index") fileIndex = atoi(next());
    else if (a == "--cache-dir") cacheDir = next();
    else if (a == "--fresh") fresh = true;
    else if (a == "--no-probe") doProbe = false;
    else if (a == "--probe-only") probeOnly = true;
  }
  if (!listFile.empty()) {
    std::ifstream in(listFile);
    std::string line;
    while (std::getline(in, line)) {
      if (!line.empty() && line[0] != '#') urls.push_back(line);
    }
  } else if (!url.empty()) {
    urls.push_back(url);
  }
  if (urls.empty()) {
    fprintf(stderr, "用法: torrentbench -i <url> [--tag X] [--probe-only] ...\n");
    return 1;
  }
  // 默认缓存目录: exe同目录 bench_cache; --fresh 先清空(测冷启动)
  if (cacheDir.empty()) {
    cacheDir = (std::filesystem::path(argv[0]).parent_path() / "bench_cache")
                   .generic_string();
  }
  if (fresh) {
    std::error_code ec;
    std::filesystem::remove_all(cacheDir, ec);
    printf("RESULT %s fresh_cache 1 %s\n", tag.c_str(), cacheDir.c_str());
  }
  ResultSink sink;
  sink.tag = tag;
  sink.open(outFile);
  // 日志落盘(无 -o 时落 exe 同目录 torrentbench.log)
  std::string logPath = outFile.empty()
      ? (std::filesystem::path(argv[0]).parent_path() / "torrentbench.log").generic_string()
      : outFile + ".log";
  LogFileOb logOb;
  if (logOb.open(logPath)) {
    setLogObserver(&logOb);
    printf("== log: %s\n", logPath.c_str());
  }
  auto seeks = parseSeeks(seeksStr);
  for (size_t i = 0; i < urls.size(); ++i) {
    std::string t = urls.size() > 1 ? tag + std::to_string(i + 1) : tag;
    printf("== bench %s : %s\n", t.c_str(), urls[i].c_str());
    fflush(stdout);
    ResultSink one;
    one.tag = t;
    one.open(outFile);
    runOne(t, urls[i], cacheDir, probeTimeoutMs, playSec, seeks, fileIndex,
           doProbe, probeOnly, one);
  }
  printf("== bench done\n");
  return 0;
}
