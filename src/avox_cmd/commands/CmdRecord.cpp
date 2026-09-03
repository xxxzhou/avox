#include "CmdRecord.h"

#include <chrono>
#include <csignal>
#include <cstdio>
#include <thread>
#ifdef _WIN32
#include <conio.h>
#endif

#include "avox_cmd/CmdHelper.hpp"
#include "avox/Avox.hpp"
#include "avox/AvoxMuxer.h"
#include "avox/AvoxSource.h"
#include "avox/module/OptionKey.hpp"
#include "avox/module/Time.hpp"
#ifdef AVOX_ENABLE_FREETYPE
#include "avox_freetype/FreetypeExport.h"
#endif

namespace avox {

// ms -> HH:MM:SS
static void fmtTime(int64_t ms, char* buf, size_t len) {
  if (ms < 0) ms = 0;
  int64_t t = ms / 1000;
  snprintf(buf, len, "%02d:%02d:%02d", (int)(t / 3600),
           (int)((t % 3600) / 60), (int)(t % 60));
}

// 录制观察者: 缓存进度 + 错误 + 完成状态
// 回调线程写，主线程读，只需最新值无需严格同步
class RecordOb : public IRecorderOb {
 public:
  void onProgress(const RecorderProgress& progress) override {
    lastProgress = progress;
  }
  void onIoError(AVError error, const char* msg) override {
    fprintf(stderr, "record error: %d %s\n", (int32_t)error, msg);
    hasError = true;
  }
  void onComplete() override { completed = true; }

  RecorderProgress lastProgress = {};
  bool completed = false;
  bool hasError = false;
};

Command cmdRecord() {
  Command cmd;
  cmd.name = "record";
  cmd.desc = "录制URL/流为视频文件 (录屏用 device -record)";
  // 输入源
  cmd.parser.addArg({"-i", "--input", ArgType::String, true, "输入源 URL", ""});
  // 输出文件
  cmd.parser.addArg({"-o", "--output", ArgType::String, false,
                     "输出文件路径 (默认自动生成到 records/ 目录)", ""});
  // 录制时长
  cmd.parser.addArg(
      {"-t", "--duration", ArgType::Int, false, "录制时长(秒), 0=无限", "0"});
  // 转码模式
  cmd.parser.addArg({"-tc", "", ArgType::Boolean, false,
                     "转码录制 (解码→处理→编码, 默认转封装)", ""});
  // 关闭视频/音频轨
  cmd.parser.addArg(
      {"-novideo", "", ArgType::Boolean, false, "关闭视频轨 (只录音频)", ""});
  cmd.parser.addArg(
      {"-noaudio", "", ArgType::Boolean, false, "关闭音频轨 (只录视频)", ""});
  // IO 方案
  cmd.parser.addArg(
      {"-io", "", ArgType::String, false,
       "IO方案: auto/ffmpeg/zlmediakit (auto=本地ffmpeg,网络zlmediakit)", ""});
  // 硬编码 (转码模式有效)
  cmd.parser.addArg(
      {"-hard", "", ArgType::Boolean, false, "硬编码 (转码模式有效)", ""});
  // RTSP拉流倍速
  cmd.parser.addArg({"-speed", "", ArgType::Number, false,
                     "RTSP拉流倍速 (仅zlmediakit IO, 点播/回放源有效)", "1.0"});
  // 日志打印
  cmd.parser.addArg(
      {"-log-packet", "", ArgType::Boolean, false, "打印IO包日志", ""});
  // 日志文件
  cmd.parser.addArg(
      {"-log-file", "", ArgType::String, false, "日志记录到文件路径", ""});

  cmd.run = [](const ParsedArgs& args) -> int {
    std::string input = args.getString("input");
    // -i 是必填输入源: 缺值由命令自己拦 (见 play 同款守卫, 不依赖 Shell 全局 -i 检查)。
    if (input.empty()) {
      fprintf(stderr, "缺少输入源: 用 -i <URL> 指定 (如 record -i rtsp://...)\n");
      return 1;
    }
    std::string output = args.getString("output", "");
    int duration = args.getInt("duration", 0);
    bool bTranscode = args.getBool("tc");
    bool noVideo = args.getBool("novideo");
    bool noAudio = args.getBool("noaudio");
    std::string ioPlanStr = args.getString("io", "");
    bool hardEncode = args.getBool("hard");
    float speed = args.getFloat("speed", 1.0f);
    bool logPacket = args.getBool("log-packet");
    std::string logFilePath = args.getString("log-file", "");
    // 解析 IoPlan
    IoPlan ioPlan;
    if (ioPlanStr == "ffmpeg") {
      ioPlan = IoPlan::ffmpeg;
    } else if (ioPlanStr == "zlmediakit") {
      ioPlan = IoPlan::zlmediakit;
    } else {
      bool isLocal = checkLocalPath(input.c_str());
      ioPlan = isLocal ? IoPlan::ffmpeg : IoPlan::zlmediakit;
      ioPlanStr = isLocal ? "ffmpeg" : "zlmediakit";
    }
    // 日志文件: 未指定则自动写到 <运行目录>/logs/<时间>.log
    if (logFilePath.empty()) {
      std::string logsDir = getAvoxPath() + "/logs";
      ensureDir(logsDir);
      logFilePath = logsDir + "/record_" + formatStamp_YMDHMS() + ".log";
    }
    FileLogOb* fileLogOb = nullptr;
    {
      fileLogOb = new FileLogOb(logFilePath);
      if (!fileLogOb->isOpen()) {
        fprintf(stderr, "无法打开日志文件: %s\n", logFilePath.c_str());
        delete fileLogOb;
        fileLogOb = nullptr;
      } else {
        setLogObserver(fileLogOb);
      }
    }
    // 确定输出路径
    if (output.empty()) {
      std::string recDir = getAvoxPath() + "/records";
      ensureDir(recDir);
      output = recDir + "/record_" + std::string(bTranscode ? "tc" : "remux") + "_" + formatStamp_YMDHMS() + ".mp4";
    } else {
      // 确保输出目录存在
      auto lastSep = output.find_last_of("/\\");
      if (lastSep != std::string::npos) {
        ensureDir(output.substr(0, lastSep));
      }
    }
    // 创建录制器
    IRecorder* recorder = createRecorder(bTranscode);
    if (!recorder) {
      fprintf(stderr, "failed to create recorder\n");
      return 1;
    }
    recorder->setIoPlan(ioPlan);
    recorder->setMuxerType(MuxerType::ffmpeg);
    // 丢弃视频/音频轨(open前),设none丢弃对应轨
    recorder->setVideoCodec(noVideo ? VCodecId::none : VCodecId::h265);
    recorder->setAudioCodec(noAudio ? ACodecId::none : ACodecId::aac);
    // RTSP拉流倍速(open前经option下发,仅zlmediakit IO生效)
    if (speed != 1.0f) {
      recorder->getOption()->setNumber(AVOX_MP_IO_RTSP_SPEED_DOUBLE, speed);
    }
    // IO包日志(open前经option链到ioSource, 检查进来的包/PTS是否就绪)
    if (logPacket) {
      recorder->getOption()->setBool(AVOX_LOG_SOURCE_INPACKET_BOOL, true);
    }
    // 转码模式: 设置离屏渲染 + 字体叠加 PTS
    ISurfaceRender* render = nullptr;
#ifdef AVOX_ENABLE_FREETYPE
    IFontLayer* fontLayer = nullptr;
#endif
    if (bTranscode) {
      render = recorder->getSurfaceRender();
      if (render) {
        YuvType yuvType = hardEncode ? YuvType::nv12 : YuvType::yuv420P;
        render->setOffSurface(yuvType);
#ifdef AVOX_ENABLE_FREETYPE
        fontLayer = enableRenderFont(render);
        if (fontLayer) {
          fontLayer->setFont("simhei.ttf", 28);
          fontLayer->setColor(0.0f, 1.0f, 0.4f, 0.0f);
          FontLayout l = {};
          l.alignment.horizontal = HAlignType::left;
          l.alignment.vertical = VAlignType::top;
          l.x = 0.02f;
          l.y = 0.02f;
          l.width = 0.4f;
          l.height = 0.04f;
          fontLayer->updateLayout(0, l);
        }
#endif
      }
    }
    // 注册观察者
    RecordOb ob = {};
    addRecorderOb(recorder, &ob);
    // 注册信号处理
    gCmdRunning = true;
    std::signal(SIGINT, cmdSignalHandler);
    std::signal(SIGTERM, cmdSignalHandler);
    // 打印配置
    printf("Recording: %s -> %s\n", input.c_str(), output.c_str());
    printf("  Mode: %s | IO: %s\n", bTranscode ? "transcode" : "remux",
           ioPlanStr.c_str());
    if (duration > 0) {
      char durStr[16];
      fmtTime((int64_t)duration * 1000, durStr, sizeof(durStr));
      printf("  Duration: %s\n", durStr);
    }
    if (bTranscode && hardEncode) printf("  Encode: hard\n");
    if (speed != 1.0f) printf("  Speed: %.1fx\n", speed);
    if (!logFilePath.empty()) printf("  log-file:  %s\n", logFilePath.c_str());
    printf("  Press Ctrl+C or ESC to stop.\n");
    // 打开录制
    if (!recorder->open(input.c_str(), output.c_str())) {
      fprintf(stderr, "failed to open recorder\n");
      removeRecorderOb(recorder, &ob);
      delete recorder;
      return 1;
    }
    // 主循环
    auto startTime = std::chrono::steady_clock::now();
    while (gCmdRunning) {
      // ESC主动停止: 与Ctrl+C同路径(跳出循环后统一close), 不在按键处直接close
#ifdef _WIN32
      if (_kbhit() && _getch() == 27) {
        printf("\nESC pressed, stopping...\n");
        break;
      }
#endif
      RecorderState state = recorder->getState();
      if (state == RecorderState::completed) {
        printf("\nRecord %s\n", ob.hasError ? "error" : "completed");
        break;
      }
      if (ob.completed) {
        printf("\nRecord completed\n");
        break;
      }
      // 时长限制
      if (duration > 0) {
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                           std::chrono::steady_clock::now() - startTime)
                           .count();
        if (elapsed >= duration) {
          printf("\nDuration limit reached (%ds)\n", duration);
          break;
        }
      }
      // 进度显示
      RecorderProgress prog = ob.lastProgress;
      if (prog.totalTimeMs > 0 && prog.currentTimeMs >= 0) {
        int percent = (int)(prog.currentTimeMs * 100 / prog.totalTimeMs);
        if (percent > 100) percent = 100;
        char curStr[16], totalStr[16];
        fmtTime(prog.currentTimeMs, curStr, sizeof(curStr));
        fmtTime(prog.totalTimeMs, totalStr, sizeof(totalStr));
        printf("\r  Progress: %d%% (%s/%s)    ", percent, curStr, totalStr);
        fflush(stdout);
      } else if (prog.currentTimeMs >= 0) {
        char curStr[16];
        fmtTime(prog.currentTimeMs, curStr, sizeof(curStr));
        printf("\r  Progress: PTS %s    ", curStr);
        fflush(stdout);
      }
      // 转码模式: 更新 PTS 字体叠加
#ifdef AVOX_ENABLE_FREETYPE
      if (fontLayer && prog.currentTimeMs >= 0) {
        char ptsStr[32];
        fmtTime(prog.currentTimeMs, ptsStr, sizeof(ptsStr));
        char text[64];
        snprintf(text, sizeof(text), "PTS: %s", ptsStr);
        fontLayer->setTextLayout(0);
        fontLayer->drawText(text);
      }
#endif
      std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    // 停止录制
    recorder->close();
    // 清理
    removeRecorderOb(recorder, &ob);
    // 清理日志观察者: 恢复调用方(如 AgentShell/ChainRunner)的 observer, 不置空
    if (fileLogOb) {
      restoreLogObserver();
      delete fileLogOb;
    }
#ifdef AVOX_ENABLE_FREETYPE
    if (fontLayer && render) {
      disableRenderFont(render);
    }
#endif
    delete recorder;
    printf("\nOutput: %s\n", output.c_str());
    return 0;
  };
  return cmd;
}

}
