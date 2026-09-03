/**
 * @file facetest.cpp
 * @brief IAudioFace 端到端接口测试 — wav2arkit_cpu (avox_avatar 插件)
 *
 * Phase1 验证: 不再直连 ONNX, 走对外接口 createAudioFace → feed → onFaceBlendshape,
 * 证实 avox_avatar 插件 + AudioReshaper(24kHz→16kHz) + OnnxSessionCache 全链路通。
 *
 *   createAudioFace(wav2arkit) → setAudioDesc(FLT/mono/srcRate) → feed(100ms 块)
 *   → onFaceDesc(30,52) + onFaceBlendshape(52×float, pts, final) → stop() 排空
 *
 * 跑两轮: 第1轮加载 385MB 模型(慢), 第2轮命中 OnnxSessionCache(测纯推理)。
 * 模型经 OnnxModel::Wav2ArkitCpu → getModelFilePath 解析, 无需 -m。
 *
 * 用法:
 *   facetest                # 默认找 tts_test.wav (kokoro 24kHz s16)
 *   facetest -i x.wav
 *   facetest -g             # 无 wav 合成 2s 测试音
 *
 * 退出码: 0=PASS (接口通 + 输出合理 + 第2轮实时), 1=FAIL
 */

#include <iostream>
#include <string>
#include <vector>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <mutex>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "avox/Avox.hpp"
#include "avox/AvoxAudio.h"  // IAudioFace / IAudioFaceOb / createAudioFace / AudioDesc
#include "avox/AvoxAvatar.h"  // getArkit52NamesCsv (canonical 名表校验)

using namespace avox;

namespace fs = std::filesystem;

// ARKit52 嘴部相关索引 — canonical 顺序 (getArkit52NamesCsv; wav2arkit 插件输出已重排, jawOpen 24→25)
constexpr int kJawOpen = 25;
constexpr int kMouthClose = 27;
constexpr int kMouthLowerDownL = 34;
constexpr int kMouthSmileL = 44;
constexpr int kMouthSmileR = 45;

struct Options {
  std::string inputWav = "tts_test.wav";
  bool generate = false;
};

void printHelp(const char* prog) {
  std::cout << "用法: " << prog << " [选项]\n\n"
            << "选项:\n"
            << "  -i, --input <wav>    输入 WAV (默认: tts_test.wav)\n"
            << "  -g, --generate       无 wav 时合成 2s 测试音\n"
            << "  -h, --help           显示帮助\n"
            << std::endl;
}

bool parseArgs(int argc, char* argv[], Options& opts) {
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "-h" || arg == "--help") { printHelp(argv[0]); return false; }
    if (arg == "-i" || arg == "--input") { if (i + 1 < argc) opts.inputWav = argv[++i]; continue; }
    if (arg == "-g" || arg == "--generate") { opts.generate = true; continue; }
    std::cerr << "未知参数: " << arg << std::endl;
    printHelp(argv[0]);
    return false;
  }
  return true;
}

std::string findWav(const Options& opts) {
  if (fs::exists(opts.inputWav)) return opts.inputWav;
  std::vector<std::string> roots;
#ifdef _WIN32
  char exePath[MAX_PATH] = {0};
  if (GetModuleFileNameA(nullptr, exePath, MAX_PATH) > 0) {
    fs::path exeDir = fs::path(exePath).parent_path();
    roots.push_back(exeDir.string());
    roots.push_back(exeDir.parent_path().string());
  }
#endif
  roots.push_back(".");
  roots.push_back(fs::current_path().string());
  for (const auto& r : roots) {
    if (r.empty() || !fs::exists(r)) continue;
    fs::path p = fs::path(r) / opts.inputWav;
    if (fs::exists(p)) return p.string();
  }
  return "";
}

uint16_t rdU16(const unsigned char* p) { return uint16_t(p[0]) | (uint16_t(p[1]) << 8); }
uint32_t rdU32(const unsigned char* p) {
  return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

// 读标准 PCM WAV (format 1, s16/s24/s32) → mono float32 [-1,1], 返回原生采样率
std::vector<float> readWavMonoF32(const std::string& path, int& outRate) {
  std::vector<float> empty;
  FILE* fp = std::fopen(path.c_str(), "rb");
  if (!fp) return empty;
  unsigned char hdr[12];
  if (std::fread(hdr, 1, 12, fp) != 12) { std::fclose(fp); return empty; }
  if (std::memcmp(hdr, "RIFF", 4) != 0 || std::memcmp(hdr + 8, "WAVE", 4) != 0) {
    std::fclose(fp);
    return empty;
  }
  int rate = 0, channels = 0, bits = 0, fmt = 0;
  std::vector<unsigned char> data;
  unsigned char chunk[8];
  while (std::fread(chunk, 1, 8, fp) == 8) {
    uint32_t csize = rdU32(chunk + 4);
    if (std::memcmp(chunk, "fmt ", 4) == 0) {
      std::vector<unsigned char> fmtData(csize);
      if (std::fread(fmtData.data(), 1, csize, fp) != csize) { std::fclose(fp); return empty; }
      if (csize >= 16) {
        fmt = rdU16(fmtData.data());
        channels = rdU16(fmtData.data() + 2);
        rate = rdU32(fmtData.data() + 4);
        bits = rdU16(fmtData.data() + 14);
      }
    } else if (std::memcmp(chunk, "data", 4) == 0) {
      data.resize(csize);
      if (std::fread(data.data(), 1, csize, fp) != csize) { std::fclose(fp); return empty; }
    } else {
      if (csize & 1) csize++;
      std::fseek(fp, long(csize), SEEK_CUR);
    }
  }
  std::fclose(fp);
  if (fmt != 1 || channels <= 0 || rate <= 0 || data.empty()) return empty;
  outRate = rate;
  size_t frames = data.size() / ((bits / 8) * channels);
  std::vector<float> mono;
  mono.reserve(frames);
  int bytes = bits / 8;
  for (size_t i = 0; i < frames; ++i) {
    float acc = 0.0f;
    for (int c = 0; c < channels; ++c) {
      const unsigned char* p = data.data() + (i * channels + c) * bytes;
      if (bits == 16) {
        acc += float(int16_t(rdU16(p))) / 32768.0f;
      } else if (bits == 32) {
        acc += float(int32_t(rdU32(p))) / 2147483648.0f;
      } else if (bits == 24) {
        int32_t s = int32_t(p[0]) | (int32_t(p[1]) << 8) | (int32_t(p[2]) << 16);
        if (s & 0x800000) s |= ~0xffffff;
        acc += float(s) / 8388608.0f;
      }
    }
    mono.push_back(acc / float(channels));
  }
  return mono;
}

// 合成 2s 类语音 (16kHz): 基频 + 共振峰 + 慢幅度包络, 让 jawOpen 有变化
std::vector<float> synthVowel(int sampleRate) {
  int n = sampleRate * 2;
  std::vector<float> out(n);
  for (int i = 0; i < n; ++i) {
    double t = double(i) / sampleRate;
    double env = 0.5 + 0.5 * std::sin(2 * M_PI * 0.7 * t);
    double f0 = 120.0, f1 = 720.0, f2 = 1240.0;
    double s = 0.4 * std::sin(2 * M_PI * f0 * t)
             + 0.2 * std::sin(2 * M_PI * f1 * t)
             + 0.1 * std::sin(2 * M_PI * f2 * t);
    out[i] = float(s * env * 0.4);
  }
  return out;
}

// IAudioFaceOb 实现: 收集 blendshape 帧 (worker 线程回调, 主线程 stop 后读)
class FaceOb : public IAudioFaceOb {
 public:
  void onFaceDesc(const FaceDesc& desc) override {
    fps = desc.fps;
    count = desc.blendshapeCount;
  }
  void onFaceBlendshape(const AvoxData& raw52, int64_t pts, bool final) override {
    if (count <= 0 || raw52.size != count * int32_t(sizeof(float))) return;
    std::lock_guard<std::mutex> lk(mtx);
    const float* p = reinterpret_cast<const float*>(raw52.data);
    frames.emplace_back(p, p + count);
    ptsVec.push_back(pts);
    if (final) ++finalCount;
  }
  void onFaceError(const char* err) override { errorMsg = err ? err : ""; }
  void reset() {
    std::lock_guard<std::mutex> lk(mtx);
    frames.clear();
    ptsVec.clear();
    finalCount = 0;
  }
  int fps = 0, count = 0;
  std::mutex mtx;
  std::vector<std::vector<float>> frames;
  std::vector<int64_t> ptsVec;
  int finalCount = 0;
  std::string errorMsg;
};

int main(int argc, char* argv[]) {
  std::cout << "=== IAudioFace 端到端测试 (avox_avatar / wav2arkit) ===" << std::endl;
  Options opts;
  if (!parseArgs(argc, argv, opts)) return 0;

  // 1. 取音频 (float32 mono, 原生采样率; AudioReshaper 内部转 16kHz)
  std::vector<float> pcm;
  int srcRate = 16000;
  std::string wavPath = findWav(opts);
  if (!wavPath.empty()) {
    std::cout << "输入: " << wavPath << std::endl;
    int rate = 0;
    pcm = readWavMonoF32(wavPath, rate);
    if (pcm.empty()) {
      std::cerr << "WAV 读取失败 (仅支持 PCM s16/s24/s32)" << std::endl;
      return 1;
    }
    srcRate = rate;
    std::cout << "WAV: " << rate << "Hz, " << pcm.size() << " 样本, "
              << (double(pcm.size()) / rate) << "s" << std::endl;
  } else if (opts.generate) {
    std::cout << "无 WAV, 合成 2s 测试音 (16kHz)" << std::endl;
    pcm = synthVowel(srcRate);
  } else {
    std::cerr << "找不到 " << opts.inputWav << " (先跑 ttstest 生成, 或加 -g 合成)" << std::endl;
    return 1;
  }

  // 2. 经对外接口创建 face (createAudioFace 内 ensureStarted 加载 avox_avatar 插件)
  IAudioFace* face = createAudioFace(AudioFaceType::wav2arkit);
  if (!face) {
    std::cerr << "FAIL: createAudioFace 返回 nullptr — avox_avatar 插件未加载?" << std::endl;
    return 1;
  }
  std::cout << "createAudioFace(wav2arkit) OK" << std::endl;
  FaceOb ob;
  addAudioFaceOb(face, &ob);
  AudioDesc desc = {};
  desc.format = AudioFormat::AVOX_AUDIO_FLT;
  desc.channels = 1;
  desc.sampleRate = srcRate;
  face->setAudioDesc(desc);

  // 3. 跑两轮: 第1轮加载模型(慢), 第2轮命中 OnnxSessionCache(测纯推理)
  //    每轮: 100ms 块 feed (pts=毫秒) → stop() 排空+join → 统计回调收集到的帧
  double audioSec = double(pcm.size()) / srcRate;
  int chunkSamples = srcRate / 10;  // 100ms
  double measuredMsPerSec = 0;
  int measuredFrames = 0;
  for (int run = 1; run <= 2; ++run) {
    ob.reset();
    auto t0 = std::chrono::steady_clock::now();
    for (size_t off = 0, i = 0; off < pcm.size(); off += chunkSamples, ++i) {
      size_t n = std::min<size_t>(chunkSamples, pcm.size() - off);
      AvoxData d = {};
      d.data = reinterpret_cast<uint8_t*>(pcm.data() + off);
      d.size = int32_t(n * sizeof(float));
      d.bRef = true;
      face->feed(d, int64_t(i) * 100);  // feed 自动 start() worker
    }
    face->stop();  // 排空已入队块 + join worker
    auto t1 = std::chrono::steady_clock::now();
    double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    if (!ob.errorMsg.empty()) {
      std::cerr << "FAIL: onFaceError: " << ob.errorMsg << std::endl;
      return 1;
    }
    int frames = int(ob.frames.size());
    int expect = int(std::ceil(30.0 * audioSec));
    std::cout << "\n--- 第" << run << "轮"
              << (run == 1 ? " (含模型加载)" : " (模型已缓存)") << " ---" << std::endl;
    std::cout << "onFaceDesc: fps=" << ob.fps << " count=" << ob.count << std::endl;
    std::cout << "收到 " << frames << " 帧 (期望 ~" << expect << "), final 包数 " << ob.finalCount
              << std::endl;
    std::cout << "耗时 " << ms << "ms = " << (ms / audioSec) << "ms/秒音频"
              << " (实时门槛 1000ms/秒)" << std::endl;
    if (run == 2) {
      measuredMsPerSec = ms / audioSec;
      measuredFrames = frames;
    }
  }

  // 4. 第2轮帧的嘴部曲线 + jawOpen 范围
  double jawMax = 0, jawMin = 1, lowerMax = 0;
  std::cout << "\n--- 嘴部曲线 (frame: jawOpen / close / smileL / lowerL) ---" << std::endl;
  std::cout << "canonical 名表: " << getArkit52NamesCsv() << std::endl;
  int step = std::max(1, measuredFrames / 20);
  std::lock_guard<std::mutex> lk(ob.mtx);
  for (int f = 0; f < measuredFrames; f += step) {
    const auto& fr = ob.frames[f];
    std::cout << "  [" << f << "] jaw=" << fr[kJawOpen]
              << " close=" << fr[kMouthClose]
              << " smileL=" << fr[kMouthSmileL]
              << " lowerL=" << fr[kMouthLowerDownL] << std::endl;
    jawMax = std::max(jawMax, double(fr[kJawOpen]));
    jawMin = std::min(jawMin, double(fr[kJawOpen]));
    lowerMax = std::max(lowerMax, double(fr[kMouthLowerDownL]));
  }
  std::cout << "jawOpen 范围: [" << jawMin << ", " << jawMax << "]"
            << "  mouthLowerDownL 最大: " << lowerMax << std::endl;

  // 5. 判定
  bool interfaceOk = (ob.count == 52) && measuredFrames > 0;
  bool realtimeOk = measuredMsPerSec < 1000.0;
  bool outputOk = (jawMax - jawMin) > 0.01 && jawMax > 0.01;
  std::cout << "\n=== 判定 ===" << std::endl;
  std::cout << "接口端到端: " << (interfaceOk ? "PASS" : "FAIL") << std::endl;
  std::cout << "实时性(第2轮): " << (realtimeOk ? "PASS" : "FAIL") << std::endl;
  std::cout << "输出合理: " << (outputOk ? "PASS" : "FAIL") << std::endl;
  if (interfaceOk && realtimeOk && outputOk) {
    std::cout << ">>> GO: avox_avatar 全链路通, 可进 Phase2 Godot 驱动 <<<" << std::endl;
    return 0;
  }
  std::cout << ">>> FAIL <<<" << std::endl;
  return 1;
}
