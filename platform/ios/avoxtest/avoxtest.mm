// avox SDK Apple 平台最小验证 app
// iOS: 全屏 CAMetalLayer。无启动参数 → 「进程内回环」全链路矩阵:
//   avox::initZmEnv + mk_*_server_start 在本进程起 ZLM 服务端(rtsp/rtmp/http/rtc),
//   IRecorder 读 bundle 内 mp4 推流到 127.0.0.1, IMediaPlayer/IRtcPlayer 依次拉流判定。
//   判定大字上屏 + 全部日志写 Documents/avoxlog.txt (devicectl copy from
//   appDataContainer 取走, 不依赖被路由隔离的局域网)。
//   启动参数带 "://" 时退回单 URL 拉流模式 (devicectl/Xcode 启动参数注入)。
// macOS: 无头离屏解码, 15 秒 TEST_DONE 退出

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <QuartzCore/QuartzCore.h>

#include "AvoxPlayer.h"
#include "AvoxLog.h"

// 进程内 ZLM 服务端 C API (只调 API, 不改第三方库)
#include "mk_mediakit.h"
namespace avox {
void initZmEnv();  // 与 SDK 内部同一份 ZLM 环境 (std::call_once 幂等)
}

// 日志: NSLog + UDP 直发 Mac + 写 Documents/avoxlog.txt (每次启动重建)
#import <sys/socket.h>
#import <netinet/in.h>
#import <arpa/inet.h>
#import <unistd.h>
static NSFileHandle* g_logFile = nil;
static void ulog(NSString* fmt, ...) NS_FORMAT_FUNCTION(1,2);
static void ulog(NSString* fmt, ...) {
  va_list ap; va_start(ap, fmt);
  NSString* msg = [[NSString alloc] initWithFormat:fmt arguments:ap];
  va_end(ap);
  NSLog(@"%@", msg);
  if (!g_logFile) {
    NSString* dir = [NSSearchPathForDirectoriesInDomains(NSDocumentDirectory,
                                                         NSUserDomainMask, YES)
                         firstObject];
    NSString* path = [dir stringByAppendingPathComponent:@"avoxlog.txt"];
    [[NSFileManager defaultManager] removeItemAtPath:path error:nil];
    [[NSFileManager defaultManager] createFileAtPath:path contents:nil attributes:nil];
    g_logFile = [NSFileHandle fileHandleForWritingAtPath:path];
  }
  if (g_logFile) {
    [g_logFile writeData:[[msg stringByAppendingString:@"\n"]
                             dataUsingEncoding:NSUTF8StringEncoding]];
  }
  int fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (fd < 0) return;
  struct sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(9999);
  addr.sin_addr.s_addr = inet_addr("192.168.68.219");
  NSString* line = [NSString stringWithFormat:@"[avox] %@\n", msg];
  sendto(fd, line.UTF8String, strlen(line.UTF8String), 0,
         (struct sockaddr*)&addr, sizeof(addr));
  close(fd);
}

using namespace avox;

#if TARGET_OS_IPHONE
#import <UIKit/UIKit.h>
#import <AVFAudio/AVFAudio.h>
#endif

#include "Common/config.h"
#include "Util/logger.h"
// 静态初始化期探针: 采样 ZLM 事件名常量在本 TU 静态构造时的状态
namespace {
struct ZlmChkStatics {
  ZlmChkStatics() {
    ulog(@"[chk-static] mediaChanged=[%s] len=%d",
         mediakit::Broadcast::kBroadcastMediaChanged.c_str(),
         (int)mediakit::Broadcast::kBroadcastMediaChanged.size());
  }
};
ZlmChkStatics g_zlmChkStatics;
}
static IMediaPlayer* g_player = nullptr;
static bool g_ioError = false;     // 用例期内发生 IO 错误则判 FAIL
static NSString* g_lastErr = nil;  // 最后一条 IO 错误消息

// ── 回环矩阵结果列表: 上屏 (iOS) + 日志 ──
static NSMutableArray<NSString*>* g_results = nil;
#if TARGET_OS_IPHONE
static UILabel* g_resultLabel = nil;

static void refreshResultLabel(void) {
  // UIKit 只能主线程碰: 后台直接改 label 会踩堆 (EXC_BAD_ACCESS 实测)
  dispatch_async(dispatch_get_main_queue(), ^{
    if (!g_resultLabel) return;
    @synchronized(g_results) {
      g_resultLabel.text = [g_results componentsJoinedByString:@"\n"];
    }
  });
}
#else
static void refreshResultLabel(void) {}
#endif
static void addResult(NSString* name, bool pass, NSString* note) {
  NSString* line =
      [NSString stringWithFormat:@"%@ %@", name, pass ? @"PASS✅" : @"FAIL❌"];
  if (note.length) line = [line stringByAppendingFormat:@" (%@)", note];
  @synchronized(g_results) { [g_results addObject:line]; }
  ulog(@"[AVOX][TEST] case=%@ result=%s%@", name, pass ? "PASS" : "FAIL",
       note.length ? [NSString stringWithFormat:@" note=%@", note] : @"");
  refreshResultLabel();
}

// 播放器状态观察: 全部走日志
class TestOb : public IMediaPlayerOb {
 public:
  void onStateChange(PlayerState pre, PlayerState cur) override {
    ulog(@"state: %s -> %s", getPlayerStateStr(pre), getPlayerStateStr(cur));
  }
  void onReady() override {
    // 不解引用 g_player: 回调可能在 close/delete 竞态窗口内到达
    ulog(@"onReady");
  }
  void onIoError(AVError err, const char* msg) override {
    g_ioError = true;
    // alloc 持有: stringWithUTF8String 是 autorelease, avox 任务线程池一排就悬空
    if (msg) g_lastErr = [[NSString alloc] initWithUTF8String:msg];
    ulog(@"ioError=%d msg=%s", (int)err, msg ? msg : "(null)");
  }
  void onDecodeError(TrackType track, DecodeResult err) override {
    ulog(@"decodeError track=%s err=%s", getTrackTypeStr(track),
         getDecodeResultStr(err));
  }
  void onComplete() override { ulog(@"onComplete"); }
};
static TestOb g_mediaOb;

static void summary(const char* tag) {
  if (!g_player) return;
  ulog(@"%s state=%s pos=%lldms duration=%lldms fps=%.1f", tag,
       getPlayerStateStr(g_player->getState()), (long long)g_player->getPosition(),
       (long long)g_player->getDuration(), g_player->getFps());
}

// ── WebRTC 无头拉流 (移植 rtcplayheadless): 连接+首帧+fps>0 即 PASS ──
#include <atomic>
#include <chrono>
#include <thread>
class RtcHeadlessOb : public IRtcEventOb {
 public:
  std::atomic<bool> connected{false};
  std::atomic<bool> firstFrame{false};
  void onConnectionState(RtcConnState state) override {
    ulog(@"[rtc] conn: %s", getRtcConnStateStr(state));
    if (state == RtcConnState::connected) connected = true;
  }
  void onFirstVideoFrame() override { firstFrame = true; }
};

static bool rtcPullOnce(const char* url) {
  ulog(@"[rtc] url: %s", url);
  IRtcPlayer* player = createWebRtcPlayer();
  if (!player) {
    ulog(@"[rtc] createWebRtcPlayer null");
    return false;
  }
  player->setRollType(RtcRollType::offer);
  player->setVideoDirection(RtpDirection::recvOnly);
  player->setAudioDirection(RtpDirection::inactive);
  RtcHeadlessOb ob;
  player->addOb(&ob);
  IRtcEventOb* sdpAgent = createZlTestSdpAgent(player, url);
  player->addOb(sdpAgent);
  player->open();
  bool pass = false;
  double fps = 0;
  for (int i = 0; i < 20 * 20; ++i) {
    if (ob.connected) {
      fps = player->getFps();
      if (ob.firstFrame && fps > 0) {
        pass = true;
        break;
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  ulog(@"[rtc] conn=%d firstFrame=%d fps=%.1f loss=%.2f rtt=%d",
       ob.connected.load(), ob.firstFrame.load(), fps, player->getLossRate(),
       player->getRttMs());
  player->removeOb(&ob);
  player->removeOb(sdpAgent);
  player->close();
  delete player;
  return pass;
}

// 单次拉流: 新开 player, 15s 内 playing+fps>0+pos>1500 判过
static bool mediaPullOnce(void* surface, const char* url) {
  ulog(@"pull open url=%s", url);
  g_ioError = false;
  g_lastErr = nil;
  g_player = createMediaPlayer();
  if (!g_player) {
    ulog(@"FATAL createMediaPlayer=nullptr");
    return false;
  }
  addMediaPlayerOb(g_player, &g_mediaOb);
  if (surface) {
    g_player->getSurfaceRender()->setSurface(surface);
  } else {
    g_player->getSurfaceRender()->setOffSurface(YuvType::yuv420P);
  }
  // IO 方案: AVOX_IO_PLAN=ffmpeg 走 ffmpeg9 拉流
  if (getenv("AVOX_IO_PLAN")) {
    g_player->setIoPlan(IoPlan::ffmpeg);
  }
  g_player->open(url);
  bool pass = false;
  for (int i = 0; i < 15 * 10; ++i) {
    if (g_player->getState() == PlayerState::playing && g_player->getFps() > 0 &&
        g_player->getPosition() > 1500) {
      pass = true;
      break;
    }
    if (g_ioError) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  summary("pull-final");
  g_player->close();
  delete g_player;
  g_player = nullptr;
  return pass;
}

// 拉流用例: 失败自动重开重试 (HLS 首片未就绪的 404 场景靠这个兜)
static void mediaPullCase(void* surface, NSString* name, NSString* url) {
  for (int attempt = 1; attempt <= 3; ++attempt) {
    if (mediaPullOnce(surface, url.UTF8String)) {
      addResult(name, true, nil);
      return;
    }
    ulog(@"case=%@ attempt=%d failed ioError=%d err=%@", name, attempt, g_ioError,
         g_lastErr ?: @"(null)");
    std::this_thread::sleep_for(std::chrono::milliseconds(2000));
  }
  addResult(name, false, g_lastErr ?: @"no frame in 15s");
}

// 推流源: IRecorder 读 bundle 内 mp4 → 推 127.0.0.1
// (MediaMuxer 未指定类型时缺省路由 zlmediakit, rtmp/rtsp URL 走 mk_pusher)
// 媒体文件定位: mainBundle (iOS) → CWD (macOS 裸可执行)
static NSString* findMedia(NSString* name, NSString* ext) {
  NSString* p = [[NSBundle mainBundle] pathForResource:name ofType:ext];
  if (p && [[NSFileManager defaultManager] fileExistsAtPath:p]) return p;
  NSString* cwd = [[NSFileManager defaultManager] currentDirectoryPath];
  p = [NSString stringWithFormat:@"%@/%@.%@", cwd, name, ext];
  return [[NSFileManager defaultManager] fileExistsAtPath:p] ? p : nil;
}

// ── 回环推流源: ffmpeg 解封装 + 按帧间 dts 节流 + annexb → mk_media ──
// (IRecorder 读本地文件是全速读, 300s 内容瞬间推完即注销; 自控节流才有直播节奏)
// 仅在配置了 ffmpeg 头 (CMake -DAVOX_FFMPEG_INCLUDE) 时编译
#if AVOXTEST_HAS_FFMPEG
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavcodec/bsf.h>
}
#endif

// 进程内原始 connect 探针: 区分「进程级网络被拦」vs「ffmpeg 内部问题」
static void probeConnect(const char* ip, int port) {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  struct sockaddr_in a = {};
  a.sin_family = AF_INET;
  a.sin_port = htons(port);
  a.sin_addr.s_addr = inet_addr(ip);
  int r = connect(fd, (struct sockaddr*)&a, sizeof(a));
  ulog(@"[probe] connect %s:%d = %d errno=%d (%s)", ip, port, r, errno,
       strerror(errno));
  close(fd);
}

#if AVOXTEST_HAS_FFMPEG
static int64_t loopNowMs(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void pushOne(const char* file, const char* stream, bool isH265) {
  AVFormatContext* ic = nullptr;
  if (avformat_open_input(&ic, file, nullptr, nullptr) < 0) {
    ulog(@"[loop] demux open failed: %s", file);
    return;
  }
  avformat_find_stream_info(ic, nullptr);
  int vi = av_find_best_stream(ic, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
  if (vi < 0) {
    ulog(@"[loop] no video stream: %s", file);
    avformat_close_input(&ic);
    return;
  }
  AVStream* vs = ic->streams[vi];
  const AVBitStreamFilter* bsf =
      av_bsf_get_by_name(isH265 ? "hevc_mp4_to_annexb" : "h264_mp4_to_annexb");
  AVBSFContext* bsfCtx = nullptr;
  if (bsf && av_bsf_alloc(bsf, &bsfCtx) >= 0) {
    avcodec_parameters_copy(bsfCtx->par_in, vs->codecpar);
    av_bsf_init(bsfCtx);
  } else {
    ulog(@"[loop] annexb bsf unavailable, %s", isH265 ? "h265" : "h264");
  }
  mk_media media = mk_media_create("__defaultVhost__", "live", stream, 0, 1, 0);
  mk_media_init_video(media, isH265 ? 1 : 0, vs->codecpar->width,
                      vs->codecpar->height, 15.0,
                      vs->codecpar->bit_rate > 0 ? (int)vs->codecpar->bit_rate
                                                 : 500000);
  mk_media_init_complete(media);
  ulog(@"[loop] push start %s -> live/%s", file, stream);
  AVRational kMs = {1, 1000};
  int64_t baseDts = AV_NOPTS_VALUE;
  int64_t baseWall = 0;
  AVPacket* pkt = av_packet_alloc();
  AVPacket* out = av_packet_alloc();
  while (av_read_frame(ic, pkt) >= 0) {
    if (pkt->stream_index != vi) {
      av_packet_unref(pkt);
      continue;
    }
    int64_t dtsMs =
        pkt->dts == AV_NOPTS_VALUE ? 0 : av_rescale_q(pkt->dts, vs->time_base, kMs);
    if (baseDts == AV_NOPTS_VALUE) {
      baseDts = dtsMs;
      baseWall = loopNowMs();
    }
    int64_t target = baseWall + (dtsMs - baseDts);
    int64_t now = loopNowMs();
    if (target > now) {
      std::this_thread::sleep_for(std::chrono::milliseconds(target - now));
    }
    if (bsfCtx && av_bsf_send_packet(bsfCtx, pkt) >= 0) {
      while (av_bsf_receive_packet(bsfCtx, out) >= 0) {
        uint64_t p = out->pts == AV_NOPTS_VALUE
                         ? 0
                         : (uint64_t)av_rescale_q(out->pts, vs->time_base, kMs);
        uint64_t d = out->dts == AV_NOPTS_VALUE
                         ? 0
                         : (uint64_t)av_rescale_q(out->dts, vs->time_base, kMs);
        if (isH265) {
          mk_media_input_h265(media, out->data, out->size, d, p);
        } else {
          mk_media_input_h264(media, out->data, out->size, d, p);
        }
        av_packet_unref(out);
      }
    }
    av_packet_unref(pkt);
  }
  ulog(@"[loop] push eof live/%s", stream);
  if (bsfCtx) av_bsf_free(&bsfCtx);
  av_packet_free(&pkt);
  av_packet_free(&out);
  avformat_close_input(&ic);
  mk_media_release(media);
}
#endif  // AVOXTEST_HAS_FFMPEG

// 判定汇总 (矩阵/回环共用)
static bool g_exitWhenDone = false;  // macOS 无头自动化: 跑完即退
static void finishSummary(void) {
  NSUInteger total = 0;
  long passCnt = 0;
  @synchronized(g_results) {
    total = g_results.count;
    for (NSString* r in g_results) {
      if ([r containsString:@"PASS"]) passCnt++;
    }
    [g_results addObject:[NSString stringWithFormat:@"ALL DONE %ld/%lu",
                                                    passCnt, (unsigned long)total]];
  }
  refreshResultLabel();
  ulog(@"[AVOX][TEST] matrix finished");
  if (g_exitWhenDone) {
    // macOS 无头自动化: 退出码反映矩阵结果 (0=全过)
    ulog(@"TEST_DONE");
    exit(passCnt == total ? 0 : 1);
  }
}

// ── 局域网矩阵: Windows ZLM 完整服务 (rtsp/rtmp/hls/ts/webrtc), 一次启动顺序跑完 ──
static void startLanMatrix(void* surface) {
  setenv("AVOX_IO_PLAN", "ffmpeg", 1);  // 拉流统一走 ffmpeg9 IO, webrtc 忽略此项
  dispatch_async(dispatch_get_global_queue(0, 0), ^{
    probeConnect("192.168.68.245", 554);   // 拉流前先探: 进程能否直连
    probeConnect("192.168.68.219", 9999);  // 对照: UDP 日志目标(无监听属正常)
    mediaPullCase(surface, @"ios-rtsp-h264",
                  @"rtsp://192.168.68.245:554/live/avox264");
    mediaPullCase(surface, @"ios-rtsp-h265", @"rtsp://192.168.68.245:554/live/avox");
    mediaPullCase(surface, @"ios-rtmp-h264",
                  @"rtmp://192.168.68.245:1935/live/avox264");
    mediaPullCase(surface, @"ios-hls-h265", @"http://192.168.68.245/live/avox/hls.m3u8");
    mediaPullCase(surface, @"ios-ts-h264", @"http://192.168.68.245/live/avox264.live.ts");
    bool rtcPass = rtcPullOnce(
        "http://192.168.68.245/index/api/webrtc?app=live&stream=avox264&type=play");
    addResult(@"ios-webrtc-h264", rtcPass, rtcPass ? nil : @"no p2p frame");
    finishSummary();
  });
}

// ── 进程内回环全链路矩阵 (surface 可为 null: 离屏解码), AVOX_MATRIX=loop 时启用 ──
static void startLoopback(void* surface) {
  setenv("AVOX_IO_PLAN", "ffmpeg", 1);  // 拉流统一走 ffmpeg9 IO, webrtc 忽略此项
  dispatch_async(dispatch_get_global_queue(0, 0), ^{
    // 1. ZLM 环境 + 服务端 (高位端口, <1024 需 root)
    avox::initZmEnv();
    int pRtsp = mk_rtsp_server_start(8554, 0);
    int pRtmp = mk_rtmp_server_start(11935, 0);
    int pHttp = mk_http_server_start(18080, 0);
    int pRtc = mk_rtc_server_start(18000);
    ulog(@"[loop] servers rtsp=%d rtmp=%d http=%d rtc=%d", pRtsp, pRtmp, pHttp,
         pRtc);
    if (!pRtsp || !pRtmp || !pHttp) {
      addResult(@"loop-servers", false, @"port bind failed");
      return;
    }
    // 2. 推流源: 节流 demux → mk_media (各自后台线程)
#if AVOXTEST_HAS_FFMPEG
    NSString* w1 = findMedia(@"wall_long", @"mp4") ?: findMedia(@"wall_1", @"mp4");
    NSString* w265 = findMedia(@"wall_265", @"mp4");
    if (w1) {
      dispatch_async(dispatch_get_global_queue(0, 0), ^{
        pushOne(w1.UTF8String, "loop264", false);
      });
    } else {
      addResult(@"loop-push264", false, @"source missing");
      finishSummary();
      return;
    }
    if (w265) {
      dispatch_async(dispatch_get_global_queue(0, 0), ^{
        pushOne(w265.UTF8String, "loop265", true);
      });
    } else {
      ulog(@"[loop] wall_265 absent, h265 case skipped");
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(3000));
    // 3. 逐用例拉流
    mediaPullCase(surface, @"ios-rtsp-h264", @"rtsp://127.0.0.1:8554/live/loop264");
    mediaPullCase(surface, @"ios-rtmp-h264",
                  @"rtmp://127.0.0.1:11935/live/loop264");
    mediaPullCase(surface, @"ios-hls-h264",
                  @"http://127.0.0.1:18080/live/loop264/hls.m3u8");
    mediaPullCase(surface, @"ios-ts-h264",
                  @"http://127.0.0.1:18080/live/loop264.live.ts");
    if (w265) {
      mediaPullCase(surface, @"ios-rtsp-h265",
                    @"rtsp://127.0.0.1:8554/live/loop265");
    } else {
      addResult(@"ios-rtsp-h265", false, @"wall_265 not found, skipped");
    }
    // 4. webrtc: http 18080 信令 + udp 18000 ICE, 全在 loopback
    // (SDK 的 mk_api 未编 ENABLE_WEBRTC 时 rtc=0, 此用例会 FAIL 属预期)
    bool rtcPass = rtcPullOnce(
        "http://127.0.0.1:18080/index/api/webrtc?app=live&stream=loop264&type=play");
    addResult(@"ios-webrtc-h264", rtcPass, rtcPass ? nil : @"no p2p frame");
#else
    (void)findMedia;  // 未编 ffmpeg 头时 findMedia 无引用
    addResult(@"loop-push264", false,
              @"compiled without ffmpeg headers (set AVOX_FFMPEG_INCLUDE)");
#endif
    // 5. 总结
    finishSummary();
  });
}

#if TARGET_OS_IPHONE

// 全屏 Metal 层 view
@interface AvoxTestView : UIView
@end
@implementation AvoxTestView
+ (Class)layerClass {
  return [CAMetalLayer class];
}
@end

@interface AppDelegate : UIResponder <UIApplicationDelegate>
@property(strong, nonatomic) UIWindow* window;
@end

@implementation AppDelegate
- (BOOL)application:(UIApplication*)application didFinishLaunchingWithOptions:(NSDictionary*)options {
  ulog(@"didFinishLaunching enter");
  ulog(@"[chk-main] mediaChanged=[%s] len=%d logEvent=[%s] len=%d",
       mediakit::Broadcast::kBroadcastMediaChanged.c_str(),
       (int)mediakit::Broadcast::kBroadcastMediaChanged.size(),
       toolkit::EventChannel::kBroadcastLogEvent.c_str(),
       (int)toolkit::EventChannel::kBroadcastLogEvent.size());
  // 矩阵要跑几分钟, 别锁屏
  [UIApplication sharedApplication].idleTimerDisabled = YES;
  self.window = [[UIWindow alloc] initWithFrame:[UIScreen mainScreen].bounds];
  AvoxTestView* view = [[AvoxTestView alloc] initWithFrame:self.window.bounds];
  view.backgroundColor = [UIColor blackColor];
  self.window.rootViewController = [[UIViewController alloc] init];
  self.window.rootViewController.view = view;
  [self.window makeKeyAndVisible];
  // iOS 音频输出需要激活会话
  ulog(@"activating audio session");
  [[AVAudioSession sharedInstance] setCategory:AVAudioSessionCategoryPlayback error:nil];
  [[AVAudioSession sharedInstance] setActive:YES error:nil];
  ulog(@"creating metal device");
  CAMetalLayer* layer = (CAMetalLayer*)view.layer;
  layer.device = MTLCreateSystemDefaultDevice();
  // 判定结果浮层 (左上, 累积列表)
  g_results = [NSMutableArray array];
  g_resultLabel = [[UILabel alloc] initWithFrame:CGRectInset(self.window.bounds, 16, 60)];
  g_resultLabel.textColor = [UIColor whiteColor];
  g_resultLabel.backgroundColor = [UIColor colorWithWhite:0 alpha:0.75];
  g_resultLabel.font = [UIFont boldSystemFontOfSize:26];
  g_resultLabel.numberOfLines = 0;
  g_resultLabel.hidden = YES;
  [self.window addSubview:g_resultLabel];

  // 启动参数里带 "://" 的当播放 URL (devicectl/Xcode 启动参数注入), 退回单
  // URL 模式; 无参数 → 默认局域网矩阵 (AVOX_MATRIX=loop 切进程内回环)
  NSString* urlArg = nil;
  for (NSString* arg in [NSProcessInfo processInfo].arguments) {
    if ([arg rangeOfString:@"://"].location != NSNotFound) {
      urlArg = arg;
      break;
    }
  }
  void* surface = (__bridge void*)layer;
  if (urlArg) {
    g_resultLabel.hidden = NO;
    ulog(@"launch url=%@", urlArg);
    bool rtcMode = [urlArg rangeOfString:@"webrtc?"].location != NSNotFound ||
                   [urlArg hasPrefix:@"webrtc://"];
    dispatch_async(dispatch_get_global_queue(0, 0), ^{
      if (rtcMode) {
        bool p = rtcPullOnce(urlArg.UTF8String);
        addResult(@"single-rtc", p, p ? nil : @"no p2p frame");
      } else {
        bool p = false;
        for (int a = 0; a < 3 && !p; ++a) {
          p = mediaPullOnce(surface, urlArg.UTF8String);
          if (!p) std::this_thread::sleep_for(std::chrono::milliseconds(2000));
        }
        addResult(@"single-url", p, g_lastErr ?: (p ? nil : @"no frame in 15s"));
      }
    });
  } else {
    g_resultLabel.hidden = NO;
    const char* mtx = getenv("AVOX_MATRIX");
    bool loopMode = mtx && strcmp(mtx, "loop") == 0;
    @synchronized(g_results) {
      [g_results addObject:loopMode ? @"loopback running..."
                                    : @"lan matrix running..."];
    }
    refreshResultLabel();
    if (loopMode) {
      startLoopback(surface);
    } else {
      startLanMatrix(surface);
    }
  }
  // 注: 不加 tick 定时器 — 主线程定时解引用 g_player 会和后台 close/delete 竞态
  return YES;
}
@end

int main(int argc, char* argv[]) {
  ulog(@"main enter");
  return UIApplicationMain(argc, argv, nil, NSStringFromClass([AppDelegate class]));
}

#else  // macOS 无头验证

int main(int argc, char* argv[]) {
  @autoreleasepool {
    g_results = [NSMutableArray array];  // addResult 依赖
    g_exitWhenDone = true;
    NSString* urlArg = nil;
    for (NSString* arg in [NSProcessInfo processInfo].arguments) {
      if ([arg rangeOfString:@"://"].location != NSNotFound) {
        urlArg = arg;
        break;
      }
    }
    const char* mtx = getenv("AVOX_MATRIX");
    bool loopMode = getenv("AVOX_LOOP") || (mtx && strcmp(mtx, "loop") == 0);
    if (urlArg) {
      // 单 URL 模式
      bool rtcMode = [urlArg rangeOfString:@"webrtc?"].location != NSNotFound ||
                     [urlArg hasPrefix:@"webrtc://"];
      dispatch_async(dispatch_get_global_queue(0, 0), ^{
        bool p = false;
        if (rtcMode) {
          p = rtcPullOnce(urlArg.UTF8String);
          addResult(@"single-rtc", p, p ? nil : @"no p2p frame");
        } else {
          for (int a = 0; a < 3 && !p; ++a) {
            p = mediaPullOnce(nullptr, urlArg.UTF8String);
            if (!p) std::this_thread::sleep_for(std::chrono::milliseconds(2000));
          }
          addResult(@"single-url", p, g_lastErr ?: (p ? nil : @"no frame"));
        }
        finishSummary();
      });
    } else if (loopMode) {
      startLoopback(nullptr);
    } else {
      startLanMatrix(nullptr);
    }
    [[NSRunLoop mainRunLoop] run];
  }
  return 0;
}

#endif
