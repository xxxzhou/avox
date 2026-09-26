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
        if (phase == 0) {
            pre.push_back(nowMs());
        } else {
            post.push_back(nowMs());
        }
    }
    void onSurface() override {}
    void onRender(const SurfaceRenderEvent*) override {}
    void onWinSizeChange(int32_t, int32_t) override {}

    std::mutex mtx;
    std::atomic<int> phase{0};
    std::vector<int64_t> pre;
    std::vector<int64_t> post;
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
        HMODULE avutil = LoadLibraryA("avutil-61.dll");
        if (avutil) {
            auto setLevel = (void (*)(int))GetProcAddress(avutil, "av_log_set_level");
            if (setLevel) {
                setLevel(fftrace ? 56 : 48);  // AV_LOG_DEBUG / AV_LOG_TRACE
                std::printf("ffmpeg log level -> %s\n", fftrace ? "TRACE" : "DEBUG");
            }
        }
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

    int64_t seekSentMs = 0;
    int64_t target = 0;
    bool seekDone = false;
    int64_t lastPrint = 0;
    int64_t lastFrames = 0;
    while (true) {
        int64_t el = nowMs() - t0;
        int64_t deadline = seekAtSec > 0 ? (seekAtSec + postSec) * 1000 : postSec * 1000;
        if (el >= deadline) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        if (el - lastPrint >= 1000) {
            lastPrint = el;
            size_t nf;
            {
                std::lock_guard<std::mutex> lock(ob.mtx);
                nf = ob.pre.size() + ob.post.size();
            }
            std::printf("t=%llds state=%s fps1s=%.1f pos=%lldms\n", (long long)(el / 1000),
                        stateStr(player->getState()), (double)(nf - lastFrames),
                        (long long)player->getPosition());
            std::fflush(stdout);
            lastFrames = (int64_t)nf;
        }
        if (!seekDone && seekAtSec > 0 && el >= seekAtSec * 1000) {
            seekDone = true;
            int64_t dur = player->getDuration();
            target = seekSec > 0 ? (int64_t)seekSec * 1000 : 0;
            if (dur > 0 && (target <= 0 || target >= dur - 5000)) {
                target = dur / 2;
            }
            ob.phase = 1;
            seekSentMs = nowMs();
            std::printf(">>> seek -> %lldms\n", (long long)target);
            std::fflush(stdout);
            player->seek(target);
        }
    }

    player->close();
    removeMediaPlayerOb(player, &sob);
    removeSurfaceRenderOb(sr, &ob);
    delete player;

    // ── 分段判定 ──
    Cadence pre = cadence(ob.pre, 0);
    // post 段跳过落位后前 3s (seek 追帧属正常突发)
    Cadence post = cadence(ob.post, 3000);
    printCadence("pre ", pre);
    printCadence("post", post);
    int64_t bufPreMs = 0, bufPostMs = 0;
    int64_t bufPreN = 0, bufPostN = 0;
    for (auto& seg : trace.bufferingSegs) {
        if (seg.first == 0) {
            bufPreN++;
            bufPreMs += seg.second;
        } else {
            bufPostN++;
            bufPostMs += seg.second;
        }
        std::printf("buffering seg phase=%d %lldms\n", seg.first, (long long)seg.second);
    }
    std::printf("buffering pre=%lld(%lldms) post=%lld(%lldms)\n", (long long)bufPreN,
                (long long)bufPreMs, (long long)bufPostN, (long long)bufPostMs);
    for (auto& l : trace.log) {
        std::printf("state %s\n", l.c_str());
    }
    bool ok = post.frames >= 200 && pre.frames >= 50 && post.gap1s == 0 &&
              bufPostN <= 1;
    std::printf("[AVOX][TEST] case=stutterprobe result=%s seekTarget=%lldms "
                "pre=%lld post=%lld post maxGap=%lldms gap>500ms=%lld gap>1s=%lld "
                "bufPost=%lld(%lldms)\n",
                ok ? "PASS" : "FAIL", (long long)target, (long long)pre.frames,
                (long long)post.frames, (long long)post.maxGap,
                (long long)post.gap500, (long long)post.gap1s,
                (long long)bufPostN, (long long)bufPostMs);
    return ok ? 0 : 1;
}
