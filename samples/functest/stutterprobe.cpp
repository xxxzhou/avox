// seek 后播放节奏探针: 量化「播一会卡一会」
// 帧到达间隔 + buffering 状态段 + 位置推进, pre/post seek 分段统计。
// 动机: NAS http 直链起播正常, seek 到中段后周期性播-停循环 (Windows 播一会卡一会,
// Mac 音频卡顿) —— 离屏 yuv 抓解码/供给侧真值, 判节奏而非判像素。
// 判定: post 段 (跳过落位后前 3s 追帧期) buffering 段数 <=1 且最大帧间隔 < 1000ms
// 用法: stutterprobe <url> [postSec] [seekAtSec] [seekSec] [-soft] [-fflog]
//   postSec 缺省 90 (seek 后观测秒数); seekAtSec 缺省 15 (0=不 seek 纯稳态);
//   seekSec 缺省 0 (=时长一半); URL 需自行百分号编码
// 判定行: [AVOX][TEST] case=stutterprobe result=PASS|FAIL ...
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "avox/AvoxPlayer.h"
#include "avox/module/OptionKey.hpp"

#ifdef _WIN32
#include <windows.h>
#endif

using namespace avox;

namespace {

int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

const char* stateStr(PlayerState s) {
    switch (s) {
        case PlayerState::none: return "none";
        case PlayerState::opening: return "opening";
        case PlayerState::ready: return "ready";
        case PlayerState::playing: return "playing";
        case PlayerState::pause: return "pause";
        case PlayerState::seek: return "seek";
        case PlayerState::buffering: return "buffering";
        case PlayerState::stopped: return "stopped";
        case PlayerState::completed: return "completed";
    }
    return "?";
}

// 状态段统计: buffering 进入/退出配对计段
struct StateTrace {
    std::mutex mtx;
    PlayerState cur = PlayerState::none;
    int64_t enterMs = 0;
    // phase: 0=pre, 1=post; 每段 {phase, ms}
    std::vector<std::pair<int, int64_t>> bufferingSegs;
    std::vector<std::string> log;

    void on(PlayerState s, int phase) {
        std::lock_guard<std::mutex> lk(mtx);
        int64_t t = nowMs();
        if (cur == PlayerState::buffering && s != PlayerState::buffering) {
            bufferingSegs.push_back({phase, t - enterMs});
        }
        if (s == PlayerState::buffering && cur != PlayerState::buffering) {
            enterMs = t;
        }
        if (s != cur) {
            char buf[128];
            std::snprintf(buf, sizeof(buf), "t=%lld phase=%d state %s -> %s",
                          (long long)t, phase, stateStr(cur), stateStr(s));
            log.push_back(buf);
        }
        cur = s;
    }
};

class FrameOb : public ISurfaceRenderOb {
 public:
    void onFrame(IImageBuffer*, YuvType) override {
        std::lock_guard<std::mutex> lock(mtx);
        int p = phase.load();
        while ((int)segs.size() <= p) {
            segs.push_back({});
        }
        segs[p].push_back(nowMs());
    }
    void onSurface() override {}
    void onRender(const SurfaceRenderEvent*) override {}
    void onWinSizeChange(int32_t, int32_t) override {}

    std::mutex mtx;
    std::atomic<int> phase{0};
    // segs[0]=起播基线, segs[k]=第k次seek之后
    std::vector<std::vector<int64_t>> segs;
};

struct Cadence {
    int64_t frames = 0;
    double spanS = 0;
    int64_t maxGap = 0;
    int64_t gap500 = 0;  // >500ms
    int64_t gap1s = 0;
    double avgFps = 0;
};

Cadence cadence(const std::vector<int64_t>& v, int64_t skipAfterMs) {
    Cadence c;
    if (v.size() < 2) {
        c.frames = (int64_t)v.size();
        return c;
    }
    size_t begin = 0;
    if (skipAfterMs > 0) {
        // 跳过起点后 skipAfterMs 内的帧 (seek 落位追帧期)
        while (begin + 1 < v.size() && v[begin + 1] - v[0] <= skipAfterMs) {
            ++begin;
        }
    }
    c.frames = (int64_t)(v.size() - begin);
    c.spanS = (v.back() - v[begin]) / 1000.0;
    if (c.spanS > 0) {
        c.avgFps = (c.frames - 1) / c.spanS;
    }
    for (size_t i = begin + 1; i < v.size(); ++i) {
        int64_t g = v[i] - v[i - 1];
        if (g > c.maxGap) {
            c.maxGap = g;
        }
        if (g > 500) {
            ++c.gap500;
        }
        if (g > 1000) {
            ++c.gap1s;
        }
    }
    return c;
}

void printCadence(const char* tag, const Cadence& c) {
    std::printf("%s: frames=%lld span=%.1fs avgFps=%.1f maxGap=%lldms gap>500ms=%lld gap>1s=%lld\n",
                tag, (long long)c.frames, c.spanS, c.avgFps, (long long)c.maxGap,
                (long long)c.gap500, (long long)c.gap1s);
    std::fflush(stdout);
}

}  // namespace

int main(int argc, char* argv[]) {
    const char* url = argc > 1 ? argv[1] : "";
    if (!url || !*url) {
        std::printf(
            "用法: stutterprobe <url> [postSec] [seekAtSec] [seekSec] [-soft] [-fflog]\n"
            "  postSec 缺省 90 (seek 后观测秒数); seekAtSec 缺省 15 (0=不 seek);\n"
            "  seekSec 缺省 0 (=时长一半)\n");
        return 2;
    }
    int postSec = argc > 2 ? std::atoi(argv[2]) : 90;
    int seekAtSec = argc > 3 ? std::atoi(argv[3]) : 15;
    int seekSec = argc > 4 ? std::atoi(argv[4]) : 0;
    std::string multiList;
    int dwellSec = 15;
    bool hard = true;
    bool fflog = false;
    bool fftrace = false;
    bool persist = false;
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "-soft") {
            hard = false;
        } else if (arg == "-hard") {
            hard = true;
        } else if (arg == "-fflog") {
            fflog = true;
        } else if (arg == "-trace") {
            fflog = true;
            fftrace = true;
        } else if (arg == "-persist") {
            persist = true;
        } else if (arg == "-multi" && i + 1 < argc) {
            multiList = argv[++i];
        } else if (arg == "-dwell" && i + 1 < argc) {
            dwellSec = std::atoi(argv[++i]);
        }
    }
    if (postSec <= 0) {
        postSec = 90;
    }

    IMediaPlayer* player = createMediaPlayer();
    if (!player) {
        std::printf("[AVOX][TEST] case=stutterprobe result=FAIL reason=createMediaPlayer-null\n");
        return 1;
    }
    player->setHardDecode(hard);
    if (persist) {
        player->getOption()->setInt(AVOX_MP_IO_HTTP_PERSISTENT_INT, 1);
        std::printf("io.http.persistent -> 1\n");
    }
    if (fflog) {
#ifdef _WIN32
        HMODULE avutil = LoadLibraryA("avutil-61.dll");
        if (avutil) {
            auto setLevel = (void (*)(int))GetProcAddress(avutil, "av_log_set_level");
            if (setLevel) {
                setLevel(fftrace ? 56 : 48);  // AV_LOG_DEBUG / AV_LOG_TRACE
                std::printf("ffmpeg log level -> %s\n", fftrace ? "TRACE" : "DEBUG");
            }
        }
#endif
    }
    ISurfaceRender* sr = player->getSurfaceRender();
    sr->setVulkan(false);
    sr->setOffSurface(YuvType::yuv420P);
    FrameOb ob;
    addSurfaceRenderOb(sr, &ob);
    StateTrace trace;
    StateTrace* tr = &trace;
    struct Sob : IMediaPlayerOb {
        StateTrace* tr;
        std::atomic<int>* phase;
        void onStateChange(PlayerState pre, PlayerState cur) override {
            tr->on(cur, phase->load());
        }
        void onIoError(AVError error, const char* msg) override {
            std::printf("[IOERR] %d %s\n", (int)error, msg ? msg : "");
            std::fflush(stdout);
        }
    } sob;
    sob.tr = tr;
    sob.phase = &ob.phase;
    addMediaPlayerOb(player, &sob);

    std::printf("mode: %s decode offscreen-yuv420P postSec=%ds seekAt=%ds\nstutterprobe open %s\n",
                hard ? "hard" : "soft", postSec, seekAtSec, url);
    std::fflush(stdout);
    int64_t t0 = nowMs();
    player->open(url);
    // 起播等待 (最多 60s, http 直链含尾部 moov 两次往返)
    {
        bool up = false;
        for (int i = 0; i < 600; i++) {
            if (player->getState() == PlayerState::playing) {
                up = true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        if (!up) {
            std::printf("[AVOX][TEST] case=stutterprobe result=FAIL reason=open-timeout\n");
            return 1;
        }
    }
    std::printf("opened in %lldms duration=%lldms\n", (long long)(nowMs() - t0),
                (long long)player->getDuration());
    std::fflush(stdout);

    // ── 统一 seek 排程: -multi t1,t2,... 为多段(拖进度条形态); 否则单次 seek ──
    std::vector<int64_t> targets;
    if (!multiList.empty()) {
        size_t bg = 0;
        while (bg <= multiList.size()) {
            size_t ed = multiList.find(',', bg);
            if (ed == std::string::npos) {
                ed = multiList.size();
            }
            if (ed > bg) {
                targets.push_back((int64_t)std::atoi(multiList.substr(bg, ed - bg).c_str()) * 1000);
            }
            bg = ed + 1;
        }
    } else if (seekAtSec > 0) {
        targets.push_back(seekSec > 0 ? (int64_t)seekSec * 1000 : 0);
    }
    {
        const int64_t dur = player->getDuration();
        for (auto& t : targets) {
            if (t <= 0 || (dur > 0 && t >= dur - 5000)) {
                t = dur / 2;
            }
        }
    }
    const int64_t firstSeekAtMs = (seekAtSec > 0 ? seekAtSec : 10) * 1000;
    const int64_t dwellMs = dwellSec * 1000;
    size_t seekIdx = 0;
    int64_t nextSeekAtMs = firstSeekAtMs;
    // 循环终点 = 首seek时刻+首次dwell, 每次seek后顺延(旧值=首seek时刻会在
    // 触发前就break)
    int64_t endTime = firstSeekAtMs + dwellMs;
    int64_t lastPrint = 0;
    int64_t lastFrames = 0;
    while (true) {
        int64_t el = nowMs() - t0;
        // 终点只在全部seek发完后生效, 否则与seek判定同刻竞态抢跑
        if (seekIdx >= targets.size() && el >= endTime) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        if (el - lastPrint >= 1000) {
            lastPrint = el;
            size_t nf = 0;
            {
                std::lock_guard<std::mutex> lock(ob.mtx);
                for (auto& s : ob.segs) {
                    nf += s.size();
                }
            }
            std::printf("t=%llds state=%s fps1s=%.1f pos=%lldms\n", (long long)(el / 1000),
                        stateStr(player->getState()), (double)(nf - lastFrames),
                        (long long)player->getPosition());
            std::fflush(stdout);
            lastFrames = (int64_t)nf;
        }
        if (seekIdx < targets.size() && el >= nextSeekAtMs) {
            ob.phase = (int)seekIdx + 1;
            std::printf(">>> seek#%zu -> %lldms\n", seekIdx, (long long)targets[seekIdx]);
            std::fflush(stdout);
            player->seek(targets[seekIdx]);
            ++seekIdx;
            nextSeekAtMs = el + dwellMs;
            endTime = el + dwellMs;
        }
    }

    player->close();
    removeMediaPlayerOb(player, &sob);
    removeSurfaceRenderOb(sr, &ob);
    delete player;

    // ── 分段判定: seg0=起播基线, segK=第k次seek后(跳过落位后3s追帧期) ──
    bool ok = true;
    for (size_t k = 0; k < ob.segs.size(); ++k) {
        Cadence c = cadence(ob.segs[k], k == 0 ? 0 : 3000);
        char tag[16];
        std::snprintf(tag, sizeof(tag), "seg%zu", k);
        printCadence(tag, c);
        int64_t bufN = 0, bufMs = 0;
        for (auto& seg : trace.bufferingSegs) {
            if (seg.first == (int)k) {
                bufN++;
                bufMs += seg.second;
                std::printf("buffering seg phase=%d %lldms\n", seg.first,
                            (long long)seg.second);
            }
        }
        std::printf("buffering %s=%lld(%lldms)\n", tag, (long long)bufN,
                    (long long)bufMs);
        if (k == 0) {
            ok = ok && c.frames >= 50;
        } else {
            // 帧数地板随观测时长走: dwell 内可用 span≈dwell-3s, 30fps 源按
            // 20fps 兜底(定值 200 在 dwell=10 时数学上不可达, 假 FAIL)。
            // bufN≤4: 连接重建/打断窗口的取段重试会拆成几记 <1s 短缓冲垫,
            // 满帧下次数无意义, 总量与断流另行把关
            const int64_t floor_ =
                std::max<int64_t>(120, (int64_t)(c.spanS * 20.0));
            ok = ok && c.frames >= floor_ && c.gap1s == 0 && bufN <= 4 &&
                 bufMs <= 1500;
        }
    }
    for (auto& l : trace.log) {
        std::printf("state %s\n", l.c_str());
    }
    std::printf("[AVOX][TEST] case=stutterprobe result=%s seeks=%zu\n",
                ok ? "PASS" : "FAIL", targets.size());
    return ok ? 0 : 1;
}
