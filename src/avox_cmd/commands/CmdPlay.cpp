#include "CmdPlay.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <ctime>
#include <memory>
#include <thread>

#include "avox_cmd/CmdHelper.hpp"
#include "avox/Avox.hpp"
#include "avox/AvoxMuxer.h"
#include "avox/module/OptionKey.hpp"
#include "avox/module/Time.hpp"
#ifdef _WIN32
#include <windows.h>
#endif
#include "avox/AvoxPlayer.h"
#include "avox/AvoxSource.h"
#include "avox/AvoxVideo.h"
#include "avox/module/LogHelper.hpp"
#include "avox_vulkan/VkExport.h"
#ifdef AVOX_ENABLE_FREETYPE
#include "avox_freetype/FreetypeExport.h"
#endif

namespace avox {

Command cmdPlay() {
  Command cmd;
  cmd.name = "play";
  cmd.desc = "播放URL/流: -i URL -t 限时 -hard 硬解 -offscreen 离屏";
  // 输入源
  cmd.parser.addArg({"-i", "--input", ArgType::String, true, "输入源 URL", ""});
  // 解码模式
  cmd.parser.addArg(
      {"-hard", "", ArgType::Boolean, false, "硬解 (默认软解)", ""});
  // IO 方案: 默认自动 (本地文件->ffmpeg, 网络源->zlmediakit)
  cmd.parser.addArg(
      {"-io", "", ArgType::String, false,
       "IO方案: auto/ffmpeg/zlmediakit (auto=本地ffmpeg,网络zlmediakit)", ""});
  // 渲染模式
  cmd.parser.addArg({"-offscreen", "", ArgType::Boolean, false,
                     "离屏渲染 (默认窗口渲染)", ""});
  // 播放时长
  cmd.parser.addArg(
      {"-t", "--duration", ArgType::Int, false, "播放时长(秒), 0=无限", "0"});
  // 播放速度
  cmd.parser.addArg(
      {"-speed", "", ArgType::Number, false, "播放速度 (默认1.0)", "1.0"});
  // 低延迟
  cmd.parser.addArg(
      {"-lowlatency", "", ArgType::Boolean, false, "低延迟模式", ""});
  cmd.parser.addArg(
      {"-delay", "", ArgType::Int, false, "播放延迟(毫秒)", "2000"});
  // RTSP 传输协议
  cmd.parser.addArg(
      {"-transport", "", ArgType::String, false, "RTSP传输: udp/tcp", ""});
  // 主时钟类型 (音频PTS异常的流可切到video绕过)
  cmd.parser.addArg({"-sync-type", "", ArgType::Int, false,
                     "主时钟(0=none 1=audio 2=video)", "1"});
  // IO 超时
  cmd.parser.addArg(
      {"-timeout", "", ArgType::Int, false, "IO超时(毫秒)", "15000"});
  // 日志打印
  cmd.parser.addArg(
      {"-log-packet", "", ArgType::Boolean, false, "打印IO包日志", ""});
  cmd.parser.addArg(
      {"-log-decode", "", ArgType::Boolean, false, "打印解码帧日志", ""});
  cmd.parser.addArg(
      {"-log-render", "", ArgType::Boolean, false, "打印渲染帧日志", ""});
  // 日志文件
  cmd.parser.addArg(
      {"-log-file", "", ArgType::String, false, "日志记录到文件路径", ""});
  // 截图: 流打开后截图到目录 (周期或单次)
  cmd.parser.addArg({"-screenshot", "", ArgType::String, false,
                     "截图保存目录 (空=不截)", ""});
  cmd.parser.addArg({"-shot-interval", "", ArgType::Int, false,
                     "周期截图间隔(毫秒), 0=不周期", "0"});
  cmd.parser.addArg({"-shot-at", "", ArgType::Int, false,
                     "单次截图: 播放位置达到此毫秒截一张, -1=不用", "-1"});

  cmd.run = [](const ParsedArgs& args) -> int {
    std::string input = args.getString("input");
    // -i 是必填输入源: 缺值 (行尾落空 -i, 或压根没写 -i) 在此由命令自己拦,
    // 不依赖 Shell 层全局 -i 检查 (assets 等命令的 -i 是布尔开关, 不能被全局拦截)。
    if (input.empty()) {
      fprintf(stderr, "缺少输入源: 用 -i <URL> 指定 (如 play -i rtsp://...)\n");
      return 1;
    }
    bool hardDecode = args.getBool("hard");
    std::string ioPlanStr = args.getString("io", "");
    bool offscreen = args.getBool("offscreen");
    int duration = args.getInt("duration", 0);
    float speed = args.getFloat("speed", 1.0f);
    bool lowlatency = args.getBool("lowlatency");
    int delay = args.getInt("delay", 2000);
    int syncType = args.getInt("sync-type", 1);
    std::string transport = args.getString("transport", "");
    int timeout = args.getInt("timeout", 15000);
    bool logPacket = args.getBool("log-packet");
    bool logDecode = args.getBool("log-decode");
    bool logRender = args.getBool("log-render");
    std::string logFilePath = args.getString("log-file", "");
    // 截图目录: 未指定则用 <运行目录>/screenshots
    std::string shotDir = args.getString("screenshot", "");
    if (shotDir.empty()) shotDir = getAvoxPath() + "/screenshots";
    ensureDir(shotDir);
    int64_t shotInterval = args.getInt("shot-interval", 0);
    if (shotInterval > 0 && shotInterval < 100) shotInterval = 100;  // 避免过频
    int64_t shotAt = args.getInt("shot-at", -1);  // 单次截图位置, -1=不用
    // 解析 IoPlan: 显式指定则用之; 否则按源类型自动 (本地文件->ffmpeg,
    // 网络源->zlmediakit)
    IoPlan ioPlan;
    if (ioPlanStr == "ffmpeg") {
      ioPlan = IoPlan::ffmpeg;
    } else if (ioPlanStr == "zlmediakit") {
      ioPlan = IoPlan::zlmediakit;
    } else if (ioPlanStr == "torrent") {
      // 磁力/torrent链接: cmdOpen 会自动路由到 torrent IO(插件已注册时)
      ioPlan = IoPlan::torrent;
    } else if (input.rfind("magnet:", 0) == 0 ||
               (input.size() > 8 &&
                input.compare(input.size() - 8, 8, ".torrent") == 0)) {
      ioPlan = IoPlan::torrent;
    } else {
      bool isLocal = checkLocalPath(input.c_str());
      ioPlan = isLocal ? IoPlan::ffmpeg : IoPlan::zlmediakit;
      ioPlanStr = isLocal ? "ffmpeg" : "zlmediakit";
    }
    // 窗口模式未显式指定日志文件 -> 自动写到 <运行目录>/logs/<时间>.log
    // 运行目录取 avox.dll 所在目录 (getAvoxPath), 不带尾部分隔符
    if (logFilePath.empty() && !offscreen) {
      std::string logsDir = getAvoxPath() + "/logs";
      ensureDir(logsDir);
      logFilePath = logsDir + "/play_" + formatStamp_YMDHMS() + ".log";
    }
    // 日志文件观察者
    FileLogOb* fileLogOb = nullptr;
    if (!logFilePath.empty()) {
      fileLogOb = new FileLogOb(logFilePath);
      if (!fileLogOb->isOpen()) {
        fprintf(stderr, "无法打开日志文件: %s\n", logFilePath.c_str());
        delete fileLogOb;
        fileLogOb = nullptr;
      } else {
        setLogObserver(fileLogOb);
      }
    }
    // 创建播放器
    IMediaPlayer* mp = createMediaPlayer();
    mp->setHardDecode(hardDecode);
    mp->setIoPlan(ioPlan);
    // 配置选项
    auto* opt = mp->getOption();
    if (lowlatency) {
      opt->setBool(AVOX_MP_LOW_LATENCY_BOOL, true);
      opt->setNumber(AVOX_MP_LL_SPEED_DOUBLE, 1.2);
    }
    opt->setInt(AVOX_MP_DELAY_MS_INT, delay);
    opt->setInt(AVOX_MP_SYNC_TYPE_INT, syncType);
    opt->setInt(AVOX_MP_IO_TIMEOUT_MS_INT, timeout);
    if (!transport.empty()) {
      opt->setString(AVOX_MP_IO_RTSP_TRANSPORT_STR, transport.c_str());
    }
    // 日志: IO包日志默认开 (比较进来的包/PTS是否就绪)
    if (logPacket) opt->setBool(AVOX_LOG_SOURCE_INPACKET_BOOL, true);
    // 解码与渲染的每帧信息意义不大,默认关闭; 需要时用 -log-decode/-log-render
    // 打开
    if (logDecode) opt->setBool(AVOX_LOG_DECODER_FRAME_BOOL, true);
    if (logRender) opt->setBool(AVOX_LOG_RENDER_FRAME_BOOL, true);
    // 渲染模式: 默认窗口渲染(setSurface(nullptr)自动创建窗口), -offscreen
    // 切换离屏
    auto* render = mp->getSurfaceRender();
    if (offscreen) {
      YuvType yuvType = hardDecode ? YuvType::nv12 : YuvType::yuv420P;
      render->setOffSurface(yuvType);
    } else {
      render->setSurface(nullptr);
    }
    // 注册信号处理
    gCmdRunning = true;
    std::signal(SIGINT, cmdSignalHandler);
    std::signal(SIGTERM, cmdSignalHandler);
    // 打印配置信息
    printf("avox_cli play\n");
    printf("  input:     %s\n", input.c_str());
    printf("  decode:    %s\n", hardDecode ? "hard" : "soft");
    printf("  io:        %s\n", ioPlanStr.c_str());
    printf("  render:    %s\n", offscreen ? "offscreen" : "surface");
    printf("  duration:  %s\n",
           duration > 0 ? std::to_string(duration).c_str() : "infinite");
    printf("  speed:     %.1f\n", speed);
    if (lowlatency) printf("  lowlatency: on\n");
    if (!transport.empty()) printf("  transport: %s\n", transport.c_str());
    if (!logFilePath.empty()) printf("  log-file:  %s\n", logFilePath.c_str());
    if (!offscreen) {
      printf(
          "  (window mode) keys: Space=pause, </>=seek10s, "
          "Up/Dn=speed, P=shot, B=lut, N=sizeScale, C=color, O=toggleOSD, "
          "R=record(transcode), "
          "T=record(remux), Q=quit\n");
      printf(
          "  basic-adjust: G=on/off, H/J=hue, W/E=brightness, "
          "A/S=contrast, Z/X=saturation, Y/U=gamma\n");
      printf("  sharpen: D=on/off, K/L=sharpness, F/V=offset\n");
      printf("  quality-enhance: M=on/off, 1/2/3=mode(Restore/Upscale2x/Upscale4x/Auto)\n");
      printf("  几何: I=装饰颜色 (跟随OSD开关)\n");
    }
    // 打开
    mp->open(input.c_str());
    // 设置速度
    if (speed != 1.0f) {
      mp->speed(speed);
    }
    // 交互式 OSD (仅窗口模式): 画面上叠加「键→动作→当前值」表, 键盘实时控制,
    // 用于人工验证 IMediaPlayer / ISurfaceRender / IMediaMuxer。借鉴
    // vkfonttest.cpp。
    bool interactive = !offscreen;
    bool paused = false;
    // 速度梯子 (↑加速 / ↓减速): 0.5 -> 1 -> 2 -> 4 -> 8 -> 16
    const float kSpeeds[] = {0.5f, 1.0f, 2.0f, 4.0f, 8.0f, 16.0f};
    int speedIdx = 1;  // 默认 1.0x
    for (int i = 0; i < 6; ++i) {
      if (std::abs(kSpeeds[i] - speed) < 1e-3f) {
        speedIdx = i;
        break;
      }
    }
    float curSpeed = kSpeeds[speedIdx];
    IMediaMuxer* muxerTc = nullptr;  // 转码录 muxer (R 键, getMuxer(true))
    IMediaMuxer* muxerRemux =
        nullptr;     // 转封装录 muxer (T 键, getMuxer(false))
    int lutIdx = 0;  // Lut 循环 (B 键, 独立): 0=off, 1=amatorka, 2=miss_etikate
    int scaleIdx =
        2;  // sizeScale 循环 (N 键, 独立): 0=1/4, 1=1/2, 2=off(1.0), 3=2
    int colorIdx = 1;                    // OSD 颜色循环 (C 键, 独立), 默认绿
    std::string lastShotPath = "ready";  // 最近一次截图路径 (P 键, OSD 显示)
    std::string recTcPath;               // 转码录输出路径 (R 键, OSD 显示)
    std::string recRemuxPath;            // 转封装录输出路径 (T 键, OSD 显示)
    // 基础图像调整 (G=开关, H/J=hue, W/E=亮度, A/S=对比度, Z/X=饱和度,
    // Y/U=伽玛)
    bool basicOn = false;
    float hueV = 0.0f;       // 色调(度), -360~360
    float brightV = 0.0f;    // 亮度, -1~1
    float contrastV = 1.0f;  // 对比度, 0~2
    float saturaV = 1.0f;    // 饱和度, 0~2
    float gammaV = 1.0f;     // 伽玛, 0~3
    // 画质增强 (M=开关, 1/2/3=输出模式循环)
    bool qualityOn = false;
    QualityEnhanceParamet qualityParam = {};
    int qualityModeIdx = 1;  // 0=Restore, 1=Upscale2x(默认, 效果好), 2=Upscale4x, 3=Auto
    // FSR实时增强 (H=开关, W=deblock循环, E=RCAS切换; G关时H/W/E走FSR逻辑)
    bool fsrOn = false;
    FSRParamet fsrParam = {};
    fsrParam.scale = FSRScale::Restore;  // 默认Restore(不放大, RCAS锐化在显示分辨率直接生效, 不依赖窗口最大化; 2x/4x需窗口>源才可见)
    float deblockVal = 0.3f;  // Guided甜点: eps≈0.0016(=0.0001+0.3*0.005), 去块充分压住块边界→人物清晰; 实测0.2-0.4佳, W循环0.1-1.0
    bool rcasOn = true;
    int fsrScaleIdx = 2;  // 0=2x, 1=4x, 2=Restore(默认)
    // 锐度 (D=开关, K/L=sharpness, F/V=offset)
    bool sharpOn = false;
    SharpenVideo sharpParam = {};  // offset=1, sharpness=0
    // 几何叠加层 (跟随OSD开关, I=装饰颜色循环)
    IGeometryLayer* geoLayer = nullptr;
    int geoColorIdx = 3;  // 默认红色(3=红, OSD 文本默认1=绿)
#ifdef AVOX_ENABLE_FREETYPE
    IFontLayer* fontLayer = nullptr;
    bool osdVisible = true;
    // OSD 颜色表 (C 键循环)
    struct OsdColor {
      float r, g, b;
      const char* name;
    };
    static const OsdColor kOsdColors[] = {
        {0.2f, 0.6f, 1.0f, "亮蓝"}, {0.0f, 1.0f, 0.4f, "绿"},
        {1.0f, 0.8f, 0.0f, "橙"},   {1.0f, 0.3f, 0.3f, "红"},
        {0.8f, 0.4f, 1.0f, "紫"},   {1.0f, 1.0f, 1.0f, "白"},
    };
    const int kOsdColorCount = sizeof(kOsdColors) / sizeof(kOsdColors[0]);
    // 几何颜色与 OSD 共用 kOsdColors, 但有独立 geoColorIdx 各自循环
    // 初始化/恢复字体排版 (28 行, 分块: 状态/播放控制/截图录屏/图像处理/quit)
    auto setupFontLayout = [&]() {
      if (!fontLayer) return;
      fontLayer->setFont("simhei.ttf", 24);
      const auto& c = kOsdColors[colorIdx];
      fontLayer->setColor(c.r, c.g, c.b, 0.0f);
      const float kY0 = 0.02f;
      const float kStep = 0.024f;
      for (int i = 0; i < 30; ++i) {
        FontLayout l = {};
        l.alignment.horizontal = HAlignType::left;
        l.alignment.vertical = VAlignType::top;
        l.x = 0.02f;
        l.y = kY0 + i * kStep;
        l.width = 0.9f;
        l.height = kStep;
        fontLayer->updateLayout(i, l);
      }
    };
    if (interactive) {
      fontLayer = enableRenderFont(render);
      if (fontLayer) setupFontLayout();
    }
    // ms -> HH:MM:SS (相对偏移, 从0起)
    auto fmtMs = [](int64_t ms, char* buf, size_t len) {
      if (ms < 0) ms = 0;
      int64_t t = ms / 1000;
      snprintf(buf, len, "%02d:%02d:%02d", (int)(t / 3600),
               (int)((t % 3600) / 60), (int)(t % 60));
    };
    // ms(epoch) -> HH:MM:SS (绝对时间, 只取时分秒, 去掉年月日)
    auto fmtAbsMs = [](int64_t ms, char* buf, size_t len) {
      time_t t = static_cast<time_t>(ms / 1000);
      struct tm* lt = gmtime(&t);
      snprintf(buf, len, "%02d:%02d:%02d", lt->tm_hour, lt->tm_min, lt->tm_sec);
    };
    // 刷新全部 OSD 行 (每 ~250ms 或按键后调用; 只 drawText, 廉价)
    // 分块顺序: 状态 / 播放控制 / 截图录屏 / 图像处理 / quit
    auto refreshOsd = [&]() {
      if (!fontLayer) return;
      char row[300];
      // ===== 状态 =====
      fontLayer->setTextLayout(0);
      fontLayer->drawText("状态");
      // 1: 统一/播放器信息 (IO方案 + 解码 + 实时帧率 + 丢包率)
      snprintf(row, sizeof(row), "[状态] io:%s dec:%s | %.1ffps | loss%.1f%%",
               ioPlanStr.c_str(), hardDecode ? "hard" : "soft", mp->getFps(),
               mp->getLossRate(TrackType::video));
      fontLayer->setTextLayout(1);
      fontLayer->drawText(row);
      // 2: 视频 源信息 (codec-desc + 实时码率)
      {
        ISourceInfo* info = mp->getSourceInfo();
        if (info && info->videoSize() > 0) {
          VTrackDesc vdesc = info->getVideoDesc(0);
          std::string vdescStr;
          string_format(vdescStr, vdesc);  // e.g. "h264-1920*1080@30-yuv420p"
          snprintf(row, sizeof(row), "[视频] %s | %.0f/%.0fKb/s",
                   vdescStr.c_str(), mp->getRate(TrackType::video, false),
                   mp->getRate(TrackType::video, true));
        } else {
          snprintf(row, sizeof(row), "[视频] -");
        }
        fontLayer->setTextLayout(2);
        fontLayer->drawText(row);
      }
      // 3: 音频 源信息 (codec-desc + 实时码率)
      {
        ISourceInfo* info = mp->getSourceInfo();
        if (info && info->audioSize() > 0) {
          ATrackDesc adesc = info->getAudioDesc(0);
          std::string adescStr;
          string_format(adescStr, adesc);  // e.g. "aac-s16-44100-2"
          snprintf(row, sizeof(row), "[音频] %s | %.0f/%.0fKb/s",
                   adescStr.c_str(), mp->getRate(TrackType::audio, false),
                   mp->getRate(TrackType::audio, true));
        } else {
          snprintf(row, sizeof(row), "[音频] -");
        }
        fontLayer->setTextLayout(3);
        fontLayer->drawText(row);
      }
      // ===== 播放控制 =====
      fontLayer->setTextLayout(4);
      fontLayer->drawText("播放控制");
      // 5: 暂停/播放 -> 当前播放器状态
      snprintf(row, sizeof(row), "[Space] 暂停/播放 : %s",
               getPlayerStateStr(mp->getState()));
      fontLayer->setTextLayout(5);
      fontLayer->drawText(row);
      // 6: 快退/快进 -> 当前位置 / 总时长
      {
        int64_t pos = mp->getPosition();  // 绝对 PTS
        int64_t start = mp->getStartTime();
        int64_t rawDur = mp->getDuration();
        char t1[16];
        fmtAbsMs(pos, t1, sizeof(t1));  // 绝对时间取 HH:MM:SS, 去掉年月日
        if (rawDur > 0) {
          char ts[16], te[16];
          fmtAbsMs(start, ts, sizeof(ts));
          fmtAbsMs(start + rawDur, te, sizeof(te));
          snprintf(row, sizeof(row), "[<-/->] 快退/快进 : %s [%s~%s]", t1, ts,
                   te);
        } else {
          snprintf(row, sizeof(row), "[<-/->] 快退/快进 : %s (LIVE)", t1);
        }
        fontLayer->setTextLayout(6);
        fontLayer->drawText(row);
      }
      // 7: 速度 (↑/↓)
      snprintf(row, sizeof(row), "[up/dn] 速度 : %.2fx", curSpeed);
      fontLayer->setTextLayout(7);
      fontLayer->drawText(row);
      // ===== 截图/录屏 =====
      fontLayer->setTextLayout(8);
      fontLayer->drawText("截图/录屏");
      // 9: 截图 -> 最近截图路径
      snprintf(row, sizeof(row), "[P] 截图 : %s",
               lastShotPath.c_str());
      fontLayer->setTextLayout(9);
      fontLayer->drawText(row);
      // 10: 转码录 (R) -> getMuxer(true)
      if (muxerTc) {
        snprintf(row, sizeof(row), "[R] 转码录 : [REC] %s",
                 recTcPath.c_str());
      } else {
        snprintf(row, sizeof(row), "[R] 转码录 : 关");
      }
      fontLayer->setTextLayout(10);
      fontLayer->drawText(row);
      // 11: 原始流录 (T) -> getMuxer(false)
      if (muxerRemux) {
        snprintf(row, sizeof(row), "[T] 原始流录 : [REC] %s",
                 recRemuxPath.c_str());
      } else {
        snprintf(row, sizeof(row), "[T] 原始流录 : 关");
      }
      fontLayer->setTextLayout(11);
      fontLayer->drawText(row);
      // ===== 图像处理 =====
      fontLayer->setTextLayout(12);
      fontLayer->drawText("图像处理");
      // 13: 隐藏OSD+几何 (O)
      fontLayer->setTextLayout(13);
      fontLayer->drawText("[O] 隐藏OSD+几何");
      // 14: OSD文本颜色 (C)
      snprintf(row, sizeof(row), "[C] 文本颜色 : %s",
               kOsdColors[colorIdx].name);
      fontLayer->setTextLayout(14);
      fontLayer->drawText(row);
      // 15: 几何装饰颜色 (I)
      snprintf(row, sizeof(row), "[I] 装饰颜色 : %s",
               kOsdColors[geoColorIdx].name);
      fontLayer->setTextLayout(15);
      fontLayer->drawText(row);
      // 16: 缩放 (N) -> enableSizeScale
      snprintf(row, sizeof(row), "[N] 缩放 : %s", scaleName(scaleIdx));
      fontLayer->setTextLayout(16);
      fontLayer->drawText(row);
      // 17: 滤镜 (B) -> enableLut
      snprintf(row, sizeof(row), "[B] 滤镜 : %s", lutName(lutIdx));
      fontLayer->setTextLayout(17);
      fontLayer->drawText(row);
      // 18: 基础调整 开关 (G) -> enableBasicAdjust/disableBasicAdjust
      snprintf(row, sizeof(row), "[G] 基础调整 : %s", basicOn ? "开" : "关");
      fontLayer->setTextLayout(18);
      fontLayer->drawText(row);
      // 19: H/J (G开=色调, G关=FSR开关/--)
      if (basicOn) {
        snprintf(row, sizeof(row), "[H/J] 色调 : %.0f", hueV);
      } else {
        snprintf(row, sizeof(row), "[H] FSR增强 : %s", fsrOn ? "开" : "关");
      }
      fontLayer->setTextLayout(19);
      fontLayer->drawText(row);
      // 20: W/E (G开=亮度, G关=FSR去块/Scale)
      if (basicOn) {
        snprintf(row, sizeof(row), "[W/E] 亮度 : %.2f", brightV);
      } else {
        const char* scaleNames[] = {"2x", "4x", "RST"};
        snprintf(row, sizeof(row), "[W] 去块:%.1f [E] %s RCAS:%s", deblockVal,
                 scaleNames[fsrScaleIdx], rcasOn ? "开" : "关");
      }
      fontLayer->setTextLayout(20);
      fontLayer->drawText(row);
      // 21: 对比度 (A/S)
      snprintf(row, sizeof(row), "[A/S] 对比度 : %.2f", contrastV);
      fontLayer->setTextLayout(21);
      fontLayer->drawText(row);
      // 22: 饱和度 (Z/X)
      snprintf(row, sizeof(row), "[Z/X] 饱和度 : %.2f", saturaV);
      fontLayer->setTextLayout(22);
      fontLayer->drawText(row);
      // 23: 伽玛 (Y/U)
      snprintf(row, sizeof(row), "[Y/U] 伽玛 : %.2f", gammaV);
      fontLayer->setTextLayout(23);
      fontLayer->drawText(row);
      // 24: Real-ESRGAN 开关 (M) -> enableQualityEnhance/disableQualityEnhance
      snprintf(row, sizeof(row), "[M] Real-ESRGAN : %s", qualityOn ? "开" : "关");
      fontLayer->setTextLayout(24);
      fontLayer->drawText(row);
      // 25: 画质模式 (1/2/3) -> Restore/Upscale2x/Upscale4x/Auto
      {
        static const char* kQualityModes[] = {"Restore(1x)", "Upscale2x", "Upscale4x", "Auto"};
        snprintf(row, sizeof(row), "[1/2/3] 画质模式 : %s", kQualityModes[qualityModeIdx]);
        fontLayer->setTextLayout(25);
        fontLayer->drawText(row);
      }
      // 26: 锐度 开关 (D) -> updateSharpen/disableSharpen
      snprintf(row, sizeof(row), "[D] 锐度 : %s", sharpOn ? "开" : "关");
      fontLayer->setTextLayout(26);
      fontLayer->drawText(row);
      // 27: 清晰度 (K/L)
      snprintf(row, sizeof(row), "[K/L] 清晰度 : %.2f", sharpParam.sharpness);
      fontLayer->setTextLayout(27);
      fontLayer->drawText(row);
      // 28: 偏移 (F/V)
      snprintf(row, sizeof(row), "[F/V] 偏移 : %d", sharpParam.offset);
      fontLayer->setTextLayout(28);
      fontLayer->drawText(row);
      // 29: quit
      fontLayer->setTextLayout(29);
      fontLayer->drawText("[Q] quit");
    };
    // 几何装饰: 在标题行左右画实心小圆 ● 标题 ●, quit 行画矩形框
    // 归一化坐标 [0,1] 在 IFontLayer 和 IGeometryLayer 间一致
    // 标题文本 layout x=0.02, fontSize=24, 1汉字≈0.01275归一化宽度@1080p
    auto drawGeoDecor = [&]() {
      if (!geoLayer) return;
      const auto& gc = kOsdColors[geoColorIdx];
      geoLayer->setColor(gc.r, gc.g, gc.b);
      geoLayer->setThreshold(0.65f);
      geoLayer->clear();
      const float kY0 = 0.02f;
      const float kStep = 0.024f;
      const float kTextX = 0.02f;  // 标题文本 layout x
      const float kDotR = 9.0f;    // 圆半径(帧px), 标题装饰用 (字号24配套)
      // 标题行: (行号, 汉字数) — 用于估算右圆位置
      // 1汉字 ≈ fontSize/帧宽 ≈ 24/1920 ≈ 0.0125 (1080p)
      struct TitleInfo {
        int row;
        int charUnits;
      };
      const TitleInfo titles[] = {
          {0, 2},   // 状态
          {4, 4},   // 播放控制
          {8, 5},   // 截图/录屏 (/算半字)
          {12, 4},  // 图像处理
      };
      for (const auto& t : titles) {
        float cy = kY0 + t.row * kStep + kStep * 0.5f;
        // 左圆: 文本起始左侧
        float leftX = kTextX - 0.009f;
        // 右圆: 文本起始 + 字宽 + 间距 (字号24: 1汉字≈0.01275)
        float rightX = kTextX + t.charUnits * 0.01275f + 0.009f;
        geoLayer->drawCircle(leftX, cy, kDotR, true);
        geoLayer->drawCircle(rightX, cy, kDotR, true);
      }
      // quit 行(29): 矩形框包住 "[Q] quit"
      // "[Q] quit" 8字符 ≈ 8*0.0063 ≈ 0.05 宽 (ASCII半字宽, 字号24)
      float quitY = kY0 + 29 * kStep;
      float quitX0 = kTextX - 0.006f;
      float quitX1 = kTextX + 8 * 0.0063f + 0.006f;
      float quitY0 = quitY + 0.003f;  // 下移一点, 对齐文字基线
      float quitY1 = quitY + kStep + 0.006f;
      geoLayer->drawRect(quitX0, quitY0, quitX1, quitY1);
    };
    // OSD + 几何 开关 (O 键): 同时显隐 OSD 字体和几何叠加
    auto toggleOsd = [&]() {
      if (osdVisible) {
        disableRenderFont(render);
        fontLayer = nullptr;
        osdVisible = false;
        if (geoLayer) {
          disableRenderGeometry(render);
          geoLayer = nullptr;
        }
        printf("  OSD off (press O to show)\n");
      } else {
        fontLayer = enableRenderFont(render);
        if (fontLayer) {
          setupFontLayout();
          refreshOsd();
        }
        osdVisible = true;
        geoLayer = enableRenderGeometry(render);
        drawGeoDecor();
        printf("  OSD on\n");
      }
    };
    if (fontLayer) refreshOsd();  // 首帧先填一次
    // 几何装饰随 OSD 一起启用并画一次 (标题圆 + quit 矩形框)
    if (interactive && osdVisible) {
      geoLayer = enableRenderGeometry(render);
      drawGeoDecor();
    }
#endif
    // 等待播放结束
    auto start = std::chrono::steady_clock::now();
    auto lastShot = start;      // 上次截图时刻 (周期截图用)
    auto lastOsd = start;       // 上次 OSD 刷新时刻
    bool shotOnceDone = false;  // 单次截图是否已完成
    // 截一帧到目录, 文件名带播放位置
    auto captureShot = [&](const char* prefix) {
      std::unique_ptr<IImageBuffer> shotBuf(createImageBuffer());
      if (!shotBuf || !render->screenShot(shotBuf.get())) return;
      int64_t pos = mp->getPosition();  // 绝对 PTS
      char shotPath[512];
      snprintf(shotPath, sizeof(shotPath), "%s/%s%lld.png", shotDir.c_str(),
               prefix, static_cast<long long>(pos));
      if (saveImagePath(shotPath, shotBuf.get())) {
        printf("  screenshot: %s (pos=%lldms)\n", shotPath,
               static_cast<long long>(pos));
      }
    };
    // P 键截图: 文件名带日期, 记录路径供 OSD 显示
    auto keyShot = [&]() {
      std::unique_ptr<IImageBuffer> shotBuf(createImageBuffer());
      if (!shotBuf || !render->screenShot(shotBuf.get())) {
        printf("  screenshot failed\n");
        return;
      }
      const char* dir = shotDir.empty() ? "." : shotDir.c_str();
      std::string path = std::string(dir) + "/shot_" + formatStamp_YMDHMS() + ".png";
      if (saveImagePath(path.c_str(), shotBuf.get())) {
        lastShotPath = path;
        printf("  screenshot: %s\n", path.c_str());
      } else {
        printf("  screenshot save failed: %s\n", path.c_str());
      }
    };
    // 当前 Lut 效果名(与 freetype 无关)
    auto lutName = [](int idx) -> const char* {
      static const char* names[] = {"关", "amatorka", "miss_etikate"};
      return names[idx];
    };
    // 当前 sizeScale 名 (idx 2 = 1.0 = 关闭)
    auto scaleName = [](int idx) -> const char* {
      static const char* names[] = {"1/4", "1/2", "关", "2"};
      return names[idx];
    };
    // 切换 Lut (B 键, 与 sizeScale 互相独立) -> 验证 ISurfaceRender::enableLut
    // 循环: off -> 1(amatorka) -> 2(miss_etikate) -> off
    auto cycleLut = [&]() {
      render->disableLut();
      lutIdx = (lutIdx + 1) % 3;
      if (lutIdx > 0) {
        LutParamet lp = {};
        lp.lutIndex = lutIdx;
        render->enableLut(lp);
      }
      printf("  Lut: %s\n", lutName(lutIdx));
    };
    // 切换 sizeScale (N 键, 与 Lut 互相独立) -> 验证
    // ISurfaceRender::enableSizeScale 循环: 1/4 -> 1/2 -> 1.0(关闭) -> 2 -> 1/4
    // ...
    auto cycleScale = [&]() {
      const float kScales[] = {0.25f, 0.5f, 1.0f, 2.0f};
      scaleIdx = (scaleIdx + 1) % 4;
      if (kScales[scaleIdx] == 1.0f) {
        render->disableSizeChange();  // 1.0 = 关闭 scale
      } else {
        render->enableSizeScale(kScales[scaleIdx]);
      }
      printf("  sizeScale: %s\n", scaleName(scaleIdx));
    };
    // 切换 OSD 颜色 (C 键, 独立)
#ifdef AVOX_ENABLE_FREETYPE
    auto cycleColor = [&]() {
      if (!fontLayer) return;
      colorIdx = (colorIdx + 1) % kOsdColorCount;
      const auto& c = kOsdColors[colorIdx];
      fontLayer->setColor(c.r, c.g, c.b, 0.0f);
      printf("  OSD color: %s\n", c.name);
    };
#endif
    // 组装当前各调整值并下发整组基础调整 -> ISurfaceRender::enableBasicAdjust
    auto applyBasic = [&]() {
      BasicAdjustParamet p;
      p.hue = hueV;
      p.brightness = brightV;
      p.contrast = contrastV;
      p.saturation = saturaV;
      p.gamma = gammaV;
      render->enableBasicAdjust(p);
    };
    // 锐度: 重发当前 SharpenVideo -> ISurfaceRender::updateSharpen
    auto applySharpen = [&]() { render->updateSharpen(sharpParam); };
    // 录制开关 (R=转码录 / T=原始流录) -> 验证 IMediaMuxer
    // 转码录 getMuxer(true): 收解码后帧, 可叠效果/转码; 原始流录
    // getMuxer(false): 直传原始包
    auto toggleRecord = [&](bool bTranscode) {
      IMediaMuxer*& m = bTranscode ? muxerTc : muxerRemux;
      std::string& p = bTranscode ? recTcPath : recRemuxPath;
      const char* mode = bTranscode ? "transcode" : "remux";
      if (m) {  // 已在录 -> 停
        m->close();
        m = nullptr;
        printf("  record %s stop\n", mode);
        return;
      }
      m = mp->getMuxer(bTranscode);
      m->setMuxerType(MuxerType::ffmpeg);
      // 录制目录: <运行目录>/records, 文件名前缀区分转码(tc)/转封装(remux)
      std::string recDir = getAvoxPath() + "/records";
      ensureDir(recDir);
      std::string path = recDir + "/record_" + std::string(bTranscode ? "tc" : "remux") + "_" + formatStamp_YMDHMS() + ".mp4";
      if (m->open(path.c_str())) {
        p = path;
        printf("  record %s start: %s\n", mode, path.c_str());
      } else {
        printf("  record %s open failed\n", mode);
        m = nullptr;
      }
    };
    while (gCmdRunning) {
#ifdef _WIN32
      // 窗口被用户关闭 (点X/Alt+F4) -> 退出播放, 回到调用方 (shell)。
      // render->getSurface() 在 Win32 窗口模式即 HWND, DestroyWindow 后
      // IsWindow 为 FALSE。
      if (interactive && render->getSurface() &&
          !IsWindow((HWND)render->getSurface())) {
        printf("Window closed\n");
        break;
      }
      // 窗口模式必须在主线程泵 Win32 消息, 否则窗口无响应、画面不刷新
      MSG msg;
      while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
          gCmdRunning = false;
          break;
        }
        if (msg.message == WM_CLOSE) {  // 用户关窗口 -> 退出
          gCmdRunning = false;
        }
        if (interactive && msg.message == WM_KEYDOWN) {
          int vk = (int)msg.wParam;
          switch (vk) {
            case 'Q':
            case VK_ESCAPE:
              gCmdRunning = false;
              break;
            case VK_SPACE:  // 暂停/恢复 -> IMediaPlayer
              paused = !paused;
              if (paused) {
                mp->pause();
              } else {
                mp->resume();
              }
              break;
            case VK_LEFT: {  // 后退 10s -> IMediaPlayer
              mp->seek(mp->getPosition() - 10000);
              break;
            }
            case VK_RIGHT: {  // 前进 10s -> IMediaPlayer
              mp->seek(mp->getPosition() + 10000);
              break;
            }
            case VK_UP: {  // 加速: 0.5->1->2->4->8->16 (到顶 16)
              if (speedIdx < 5) {
                ++speedIdx;
                curSpeed = kSpeeds[speedIdx];
                mp->speed(curSpeed);
              }
              break;
            }
            case VK_DOWN: {  // 减速: 16->8->4->2->1->0.5 (到底 0.5)
              if (speedIdx > 0) {
                --speedIdx;
                curSpeed = kSpeeds[speedIdx];
                mp->speed(curSpeed);
              }
              break;
            }
            case 'P':
              keyShot();
              break;
            case 'B':  // Lut 循环 (独立) -> enableLut
              cycleLut();
              break;
            case 'N':  // sizeScale 循环 (独立) -> enableSizeScale
              cycleScale();
              break;
            case 'C':  // OSD 颜色循环 (独立)
#ifdef AVOX_ENABLE_FREETYPE
              cycleColor();
#endif
              break;
            case 'O':  // 切换 OSD 文字显示
#ifdef AVOX_ENABLE_FREETYPE
              toggleOsd();
#else
              printf("  OSD not available\n");
#endif
              break;
            case 'R':  // 转码录 -> getMuxer(true)
              toggleRecord(true);
              break;
            case 'T':  // 转封装录 -> getMuxer(false)
              toggleRecord(false);
              break;
            case 'G': {  // 基础图像调整 开/关
              basicOn = !basicOn;
              if (basicOn) {
                applyBasic();
                printf("  basicAdjust on\n");
              } else {
                render->disableBasicAdjust();
                printf("  basicAdjust off\n");
              }
              break;
            }
            case 'H': {  // G开:色调- / G关:FSR开关
              if (basicOn) {
                hueV -= 15.0f;
                applyBasic();
                printf("  hue: %.0f\n", hueV);
              } else {
                fsrOn = !fsrOn;
                if (fsrOn) {
                  fsrParam.deblockStrength = deblockVal;
                  fsrParam.enableRCAS = rcasOn;
                  render->enableFSR(fsrParam);
                  printf("  FSR on (scale=2x, deblock=%.1f, rcas=%d)\n",
                         fsrParam.deblockStrength, fsrParam.enableRCAS);
                } else {
                  render->disableFSR();
                  printf("  FSR off\n");
                }
              }
              break;
            }
            case 'J': {  // 色调 +
              hueV += 15.0f;
              if (basicOn) applyBasic();
              printf("  hue: %.0f\n", hueV);
              break;
            }
            case 'W': {  // G开:亮度- / G关:FSR deblock循环
              if (basicOn) {
                brightV = std::max(brightV - 0.1f, -1.0f);
                applyBasic();
                printf("  brightness: %.2f\n", brightV);
              } else {
                deblockVal += 0.1f;
                if (deblockVal > 1.0f) deblockVal = 0.0f;
                if (fsrOn) {
                  fsrParam.deblockStrength = deblockVal;
                  render->enableFSR(fsrParam);
                }
                printf("  FSR deblock: %.1f\n", deblockVal);
              }
              break;
            }
            case 'E': {  // G开:亮度+ / G关:FSR scale模式循环
              if (basicOn) {
                brightV = std::min(brightV + 0.1f, 1.0f);
                applyBasic();
                printf("  brightness: %.2f\n", brightV);
              } else {
                fsrScaleIdx = (fsrScaleIdx + 1) % 3;
                const char* scaleNames[] = {"2x", "4x", "Restore"};
                FSRScale scales[] = {FSRScale::Upscale2x, FSRScale::Upscale4x, FSRScale::Restore};
                fsrParam.scale = scales[fsrScaleIdx];
                if (fsrOn) {
                  fsrParam.deblockStrength = deblockVal;
                  fsrParam.enableRCAS = rcasOn;
                  render->enableFSR(fsrParam);
                }
                printf("  FSR scale: %s\n", scaleNames[fsrScaleIdx]);
              }
              break;
            }
            case 'A': {  // 对比度 -
              contrastV = std::max(contrastV - 0.1f, 0.0f);
              if (basicOn) applyBasic();
              printf("  contrast: %.2f\n", contrastV);
              break;
            }
            case 'S': {  // 对比度 +
              contrastV = std::min(contrastV + 0.1f, 2.0f);
              if (basicOn) applyBasic();
              printf("  contrast: %.2f\n", contrastV);
              break;
            }
            case 'Z': {  // 饱和度 -
              saturaV = std::max(saturaV - 0.1f, 0.0f);
              if (basicOn) applyBasic();
              printf("  saturation: %.2f\n", saturaV);
              break;
            }
            case 'X': {  // 饱和度 +
              saturaV = std::min(saturaV + 0.1f, 2.0f);
              if (basicOn) applyBasic();
              printf("  saturation: %.2f\n", saturaV);
              break;
            }
            case 'Y': {  // 伽玛 -
              gammaV = std::max(gammaV - 0.1f, 0.0f);
              if (basicOn) applyBasic();
              printf("  gamma: %.2f\n", gammaV);
              break;
            }
            case 'U': {  // 伽玛 +
              gammaV = std::min(gammaV + 0.1f, 3.0f);
              if (basicOn) applyBasic();
              printf("  gamma: %.2f\n", gammaV);
              break;
            }
            case 'D': {  // 锐度 开/关
              sharpOn = !sharpOn;
              if (sharpOn) {
                render->updateSharpen(sharpParam);
                printf("  sharpen on\n");
              } else {
                render->disableSharpen();
                printf("  sharpen off\n");
              }
              break;
            }
            case 'K': {  // sharpness -
              sharpParam.sharpness =
                  std::max(sharpParam.sharpness - 0.1f, -4.0f);
              if (sharpOn) applySharpen();
              printf("  sharpness: %.2f\n", sharpParam.sharpness);
              break;
            }
            case 'L': {  // sharpness +
              sharpParam.sharpness =
                  std::min(sharpParam.sharpness + 0.1f, 4.0f);
              if (sharpOn) applySharpen();
              printf("  sharpness: %.2f\n", sharpParam.sharpness);
              break;
            }
            case 'F': {  // offset -
              sharpParam.offset = std::max(sharpParam.offset - 1, 1);
              if (sharpOn) applySharpen();
              printf("  offset: %d\n", sharpParam.offset);
              break;
            }
            case 'V': {  // offset +
              sharpParam.offset = std::min(sharpParam.offset + 1, 8);
              if (sharpOn) applySharpen();
              printf("  offset: %d\n", sharpParam.offset);
              break;
            }
            case 'I': {  // 几何颜色循环 (独立于 OSD 文本颜色)
              geoColorIdx = (geoColorIdx + 1) % kOsdColorCount;
              drawGeoDecor();
              printf("  几何颜色: %s\n", kOsdColors[geoColorIdx].name);
              break;
            }
            case 'M': {  // 画质增强 开/关
              qualityOn = !qualityOn;
              if (qualityOn) {
                static const QualityOutputMode kModes[] = {
                    QualityOutputMode::Restore, QualityOutputMode::Upscale2x,
                    QualityOutputMode::Upscale4x, QualityOutputMode::Auto};
                qualityParam.outputMode = kModes[qualityModeIdx];
                render->enableQualityEnhance(qualityParam);
                printf("  qualityEnhance on (mode=%d)\n", qualityModeIdx);
              } else {
                render->disableQualityEnhance();
                printf("  qualityEnhance off\n");
              }
              break;
            }
            case '1': {  // 画质模式: Restore
              qualityModeIdx = 0;
              qualityParam.outputMode = QualityOutputMode::Restore;
              if (qualityOn) render->enableQualityEnhance(qualityParam);
              printf("  qualityMode: Restore(1x)\n");
              break;
            }
            case '2': {  // 画质模式: Upscale2x
              qualityModeIdx = 1;
              qualityParam.outputMode = QualityOutputMode::Upscale2x;
              if (qualityOn) render->enableQualityEnhance(qualityParam);
              printf("  qualityMode: Upscale2x\n");
              break;
            }
            case '3': {  // 画质模式: Upscale4x
              qualityModeIdx = 2;
              qualityParam.outputMode = QualityOutputMode::Upscale4x;
              if (qualityOn) render->enableQualityEnhance(qualityParam);
              printf("  qualityMode: Upscale4x\n");
              break;
            }
            default:
              break;
          }
        }
        // 打印播放时长       
        // int64_t playTime = mp->getPosition() - mp->getStartTime();
        // log(LogLevel::info, "play time:", playTime);
        TranslateMessage(&msg);
        DispatchMessage(&msg);
      }
#endif
      // 截图: 单次(播放位置达 shot-at) + 周期(间隔 shot-interval)
      if (!shotDir.empty()) {
        int64_t pos = mp->getPosition();  // 绝对 PTS
        if (shotAt >= 0 && !shotOnceDone && pos >= shotAt) {
          shotOnceDone = true;
          captureShot("shot_at_");
        }
        if (shotInterval > 0 &&
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - lastShot)
                    .count() >= shotInterval) {
          lastShot = std::chrono::steady_clock::now();
          captureShot("shot_");
        }
      }
#ifdef AVOX_ENABLE_FREETYPE
      // OSD 表刷新 (~250ms): 27 行键→动作→当前值。按键改了状态后下一拍即更新。
      if (fontLayer && std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now() - lastOsd)
                               .count() >= 250) {
        lastOsd = std::chrono::steady_clock::now();
        refreshOsd();
      }
#endif
      auto state = mp->getState();
      if (state == PlayerState::completed || state == PlayerState::stopped) {
        printf("Playback %s\n",
               state == PlayerState::completed ? "completed" : "stopped");
        break;
      }
      if (duration > 0) {
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                           std::chrono::steady_clock::now() - start)
                           .count();
        if (elapsed >= duration) {
          printf("Duration limit reached (%ds)\n", duration);
          break;
        }
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    // 停止录制 (两种 muxer 各自关闭)
    if (muxerTc) {
      muxerTc->close();
    }
    if (muxerRemux) {
      muxerRemux->close();
    }
    mp->close();
    delete mp;  // createMediaPlayer 返回需手动释放的内存,
                // 否则泄露到进程退出才清
    mp = nullptr;
    // 清理日志观察者: 恢复调用方(如 AgentShell/ChainRunner)的 observer, 不置空
    if (fileLogOb) {
      restoreLogObserver();
      delete fileLogOb;
    }
    return 0;
  };
  return cmd;
}

}
