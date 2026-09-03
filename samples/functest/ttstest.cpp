/**
 * @file ttstest.cpp
 * @brief 语音合成 (TTS) 测试程序 — IAudioTts 纯接口 (sherpa kokoro 后端)
 *
 * 验证 TTS 后端在脱离 Godot/agent 的最小环境下能否正常合成:
 *   createAudioTts(sherpa) → start() (worker 异步加载 kokoro 模型)
 *   → synthesize(文本入队) → IAudioTtsOb 收 s16 PCM → IWavSave 写 WAV
 *
 * synthesize 为异步 (文本入 textQueue, worker 线程合成), 需轮询
 * onTtsAudio(final=true) 或 onTtsError; 模型加载 + 合成耗时较长
 * (kokoro 327MB), 默认超时 180s。
 *
 * 用法:
 *   ttstest                       # 默认中英测试句, 输出 tts_test.wav
 *   ttstest "你好,这是语音合成测试"   # 自定义文本 (建议含中文, 验证 lexicon)
 *   ttstest "Hello world" -o out.wav -s 0 -p 1.0 -t 180
 *   ttstest -h
 *
 * 退出码: 0=成功且 PCM 非空, 1=失败/超时/无声
 */

#include <iostream>
#include <string>
#include <chrono>
#include <thread>
#include <atomic>
#include <cstdint>
#include <cstdlib>

#include "avox/Avox.hpp"
#include "avox/AvoxAudio.h"

using namespace avox;

// 收集合成 PCM 并落盘 WAV。回调在 sherpa worker 线程触发, 故完成/错误用 atomic
class TtsCollector : public IAudioTtsOb {
 public:
  explicit TtsCollector(std::string outPath) : outputPath(std::move(outPath)) {
    wav = createWavSave();
  }
  ~TtsCollector() override {
    if (wav) {
      if (bOpened) wav->close();
      delete wav;
    }
  }
  // 合成开始: 输出格式就绪, 打开 WAV 并写入头
  void onTtsDesc(const AudioDesc& desc) override {
    sampleRate = desc.sampleRate;
    channels = desc.channels;
    std::cout << "[desc] sampleRate=" << desc.sampleRate
              << " channels=" << static_cast<int>(desc.channels) << std::endl;
    if (wav) {
      bOpened = wav->openUrl(outputPath.c_str());
      wav->setAudioDesc(desc);
    }
  }
  // PCM 块就绪: 累积 + 写盘; final=true 表示本次 synthesize 的最后一块
  void onTtsAudio(const AvoxData& raw, int64_t pts, bool final) override {
    (void)pts;
    if (raw.size > 0 && raw.data) {
      totalBytes += raw.size;
      chunkCount++;
      if (wav && bOpened) wav->addFrame(raw);
    }
    if (final) {
      bDone.store(true);
      std::cout << "[audio] final chunk (#" << chunkCount << ")" << std::endl;
    }
  }
  void onTtsError(const char* err) override {
    errorMsg = err ? err : "unknown tts error";
    bError.store(true);
    std::cerr << "[error] " << errorMsg << std::endl;
  }
  std::string outputPath;
  IWavSave* wav = nullptr;
  bool bOpened = false;
  int32_t sampleRate = 0;
  uint16_t channels = 0;
  int64_t totalBytes = 0;
  int32_t chunkCount = 0;
  std::atomic<bool> bDone{false};
  std::atomic<bool> bError{false};
  std::string errorMsg;
};

struct Options {
  std::string text = "你好,这是语音合成测试。Hello, this is a TTS test.";
  bool textIsDefault = true;
  std::string output = "tts_test.wav";
  int speaker = 0;
  float speed = 1.0f;
  int level = 1;       // 0=mini, 1=base, 2=high
  int timeoutSec = 180;
};

void printHelp(const char* prog) {
  std::cout << "用法: " << prog << " [文本] [选项]\n\n"
            << "选项:\n"
            << "  -o, --output <path>   输出 WAV 路径 (默认: tts_test.wav)\n"
            << "  -s, --speaker <id>    说话人 ID (默认: 0)\n"
            << "  -p, --speed <f>       语速 (默认: 1.0)\n"
            << "  -l, --level <n>       模型级别 0/1/2 (默认: 1)\n"
            << "  -t, --timeout <sec>   合成超时秒数 (默认: 180)\n"
            << "  -h, --help            显示帮助\n"
            << std::endl;
}

bool parseArgs(int argc, char* argv[], Options& opts) {
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "-h" || arg == "--help") { printHelp(argv[0]); return false; }
    if (arg == "-o" || arg == "--output") { if (i + 1 < argc) opts.output = argv[++i]; continue; }
    if (arg == "-s" || arg == "--speaker") { if (i + 1 < argc) opts.speaker = std::atoi(argv[++i]); continue; }
    if (arg == "-p" || arg == "--speed") { if (i + 1 < argc) opts.speed = static_cast<float>(std::atof(argv[++i])); continue; }
    if (arg == "-l" || arg == "--level") { if (i + 1 < argc) opts.level = std::atoi(argv[++i]); continue; }
    if (arg == "-t" || arg == "--timeout") { if (i + 1 < argc) opts.timeoutSec = std::atoi(argv[++i]); continue; }
    // 首个非选项参数视为文本
    if (arg[0] != '-' && opts.textIsDefault) { opts.text = argv[i]; opts.textIsDefault = false; continue; }
    std::cerr << "未知参数: " << arg << std::endl;
    printHelp(argv[0]);
    return false;
  }
  return true;
}

int main(int argc, char* argv[]) {
  std::cout << "=== 语音合成测试 (IAudioTts / sherpa kokoro) ===" << std::endl;
  Options opts;
  if (!parseArgs(argc, argv, opts)) return 0;

  std::cout << "文本:   " << opts.text << std::endl;
  std::cout << "输出:   " << opts.output << std::endl;
  std::cout << "参数:   speaker=" << opts.speaker << " speed=" << opts.speed
            << " level=" << opts.level << " timeout=" << opts.timeoutSec << "s"
            << std::endl;

  // 1. 创建合成器 (内部 ensureStarted 扫描 plugins/, 加载 avox_sherpa 注册 sherpa key)
  IAudioTts* tts = createAudioTts(AudioTtsType::sherpa);
  if (!tts) {
    std::cerr << "createAudioTts(sherpa) 返回 nullptr — avox_sherpa 插件未加载?\n"
              << "确认 plugins/avox_sherpa.dll 与 avox.dll 同目录层级" << std::endl;
    return 1;
  }
  TtsCollector collector(opts.output);
  addAudioTtsOb(tts, &collector);

  // 2. 配置 (kokoro Phase1 固定 s16 mono @ 模型原生采样率, 忽略 setAudioDesc)
  ModelLevel level = ModelLevel::base;
  if (opts.level == 0) level = ModelLevel::mini;
  else if (opts.level == 2) level = ModelLevel::high;
  tts->setModelLevel(level);
  tts->setSpeed(opts.speed);
  tts->setSpeaker(opts.speaker);

  // 3. 启动 worker (异步加载 kokoro 模型, 首次加载 327MB 较慢)
  std::cout << "启动合成器 (异步加载 kokoro 模型, 首次较慢)..." << std::endl;
  tts->start();

  // 4. 合成 (文本入队, worker 线程排队处理)
  std::cout << "synthesize..." << std::endl;
  tts->synthesize(opts.text.c_str());

  // 5. 轮询等 final / error (异步合成, synthesize 返回不代表 PCM 就绪)
  auto t0 = std::chrono::steady_clock::now();
  while (!collector.bDone.load() && !collector.bError.load()) {
    auto dt = std::chrono::duration_cast<std::chrono::seconds>(
                  std::chrono::steady_clock::now() - t0)
                  .count();
    if (dt >= opts.timeoutSec) {
      std::cerr << "超时 (" << opts.timeoutSec << "s 未收到完整音频)" << std::endl;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
  }

  // 6. 停止合成 (置 cancelFlag + join worker, 模型常驻不卸)
  tts->stop();
  removeAudioTtsOb(tts, &collector);
  delete tts;

  // 7. 结果判定
  if (collector.bError.load()) {
    std::cerr << "合成失败: " << collector.errorMsg << std::endl;
    std::cerr << "(可能原因: kokoro 模型未下载/路径不对; 插件 ABI 不匹配)" << std::endl;
    return 1;
  }
  if (!collector.bDone.load()) {
    std::cerr << "未收到完整音频 (超时)" << std::endl;
    return 1;
  }
  // 时长: s16 每样本 2 字节; bytes/(2*channels) = 单声道样本数
  double seconds = 0.0;
  if (collector.sampleRate > 0 && collector.channels > 0) {
    seconds = static_cast<double>(collector.totalBytes) /
              (2.0 * collector.channels * collector.sampleRate);
  }
  std::cout << "\n--- 结果 ---" << std::endl;
  std::cout << "chunks=" << collector.chunkCount
            << " bytes=" << collector.totalBytes
            << " sampleRate=" << collector.sampleRate
            << " channels=" << collector.channels
            << " 时长约=" << seconds << "s" << std::endl;
  if (collector.totalBytes == 0) {
    std::cerr << "警告: PCM 为空 (无声!) — 中文请检查 SherpaTts lexicon-zh.txt 配置"
              << std::endl;
    return 1;
  }
  std::cout << "WAV 已写: " << opts.output << std::endl;
  std::cout << "=== 测试完成 ===" << std::endl;
  return 0;
}
