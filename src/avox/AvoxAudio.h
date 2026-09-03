#pragma once
#include <stdint.h>

#include "AvoxBuffer.h"
#include "AvoxBase.h"

namespace avox {

// ============== 音频格式 ==============
// 用来确定音频数据是大端还是小端，大端人眼顺序，小端机器更快
// 每字节8位的顺序，32位整数0x12345678，小端0x78、0x56、0x34、0x12（最高地址）
// 所以只有大于8位，小端大端才有意义
#define AVOX_AUDIO_MASK_ENDIAN (1 << 12)

#define AVOX_MAP_AUDIO_FMT(XX)          \
  XX(AVOX_AUDIO_U8, 0, 1, 0, "u8")      \
  XX(AVOX_AUDIO_S16, 1, 2, 0, "s16")    \
  XX(AVOX_AUDIO_S32, 2, 4, 0, "s32")    \
  XX(AVOX_AUDIO_S64, 3, 8, 0, "s64")    \
  XX(AVOX_AUDIO_FLT, 4, 4, 0, "flt")    \
  XX(AVOX_AUDIO_DBL, 5, 8, 0, "dbl")    \
  XX(AVOX_AUDIO_U8P, 6, 1, 1, "u8p")    \
  XX(AVOX_AUDIO_S16P, 7, 2, 1, "s16p")  \
  XX(AVOX_AUDIO_S32P, 8, 4, 1, "s32p")  \
  XX(AVOX_AUDIO_S64P, 9, 8, 1, "s64p")  \
  XX(AVOX_AUDIO_FLTP, 10, 4, 1, "fltp") \
  XX(AVOX_AUDIO_DBLP, 11, 8, 1, "dblp")

enum class AudioFormat : int16_t {
  other = -1,
#define XX(name, value, size, plane, str) name = value,
  AVOX_MAP_AUDIO_FMT(XX)
#undef XX
};

struct AudioDesc {
  uint16_t channels = 0;
  AudioFormat format = AudioFormat::other;
  int32_t sampleRate = 0;
  inline bool operator==(const AudioDesc& right) const {
    return channels == right.channels && format == right.format &&
           sampleRate == right.sampleRate;
  }
  inline bool operator!=(const AudioDesc& right) const {
    return !(*this == right);
  }
  bool bValid() const {
    return channels > 0 && sampleRate > 0 && format != AudioFormat::other;
  }
};

struct AudioAec {
  int32_t mobileMode = 0;
};

// 音频 tap 观察者:从 IAudioRender 读取正在播放的音频数据
// onFrame 在 tap 专用线程触发,帧仅在回调内有效,上层需延后处理请自行复制
class IAudioTapOb {
 public:
  IAudioTapOb() = default;
  virtual ~IAudioTapOb() = default;

 public:
  // tap 打开后,源格式就绪时回调一次(随后开始 onFrame)
  virtual void onAudioDesc(const AudioDesc& desc) {}
  // 定长音频块就绪(已重采样/切片到 openTap 指定的 outDesc/frameMs)
  virtual void onFrame(const AvoxData& raw, int64_t pts) {}
};

// WAV 文件保存器:将 PCM 音频数据写入可播放的 WAV 文件
// 可配合 IAudioTapOb 使用,但无继承关系
class IWavSave {
 public:
  IWavSave() = default;
  virtual ~IWavSave() = default;

 public:
  // 打开 WAV 文件
  virtual bool openUrl(const char* path) = 0;
  // 设置音频格式,addFrame 的数据应与此格式一致
  virtual bool setAudioDesc(const AudioDesc& desc) = 0;
  // 追加一帧 PCM 数据,planar 格式会自动转 interleaved
  virtual void addFrame(const AvoxData& raw) = 0;
  // 关闭文件并回填 WAV 头中的文件大小
  virtual void close() = 0;
};

class IAudioRender {
 public:
  virtual ~IAudioRender() = default;
  virtual void setVolume(float volume) = 0;
  virtual float getVolume() = 0;
  virtual void enableAec(const AudioAec& aec) {}
  virtual void disableAec() {}
  // 打开音频 tap:outDesc 为空(sampleRate=0)时跟随源格式不重采样
  // frameMs 为切片时长。默认关闭,open 后才开始备份与回调
  virtual void openTap(const AudioDesc& outDesc, int32_t frameMs) {}
  virtual void closeTap() {}
};

// ============== 语音识别 ==============

// 识别器类型
enum class RecognizerType { none, streaming, offline };

// 语音识别器后端类型(创建实例用,内部映射到工厂表字符串 key)
enum class AudioSttType { none, sherpa };

// 语音识别结果（纯值类型，可跨 ABI 导出）
struct SttResult {
  int64_t startPts = 0;
  int64_t endPts = 0;
  Language lang = Language::none;
  bool isFinal = false;
};

// 语音识别回调接口
class IAudioSttOb {
 public:
  virtual ~IAudioSttOb() = default;
  // 最终识别结果，text 在回调内有效
  virtual void onResult(const SttResult& result, const char* text) = 0;
  // 部分识别结果（流式中间结果）
  virtual void onPartialResult(const char* text) {}
  // 端点检测回调
  virtual void onEndpoint() {}
};

// 语音识别器接口（对外导出，其他语言通过此接口使用 STT）
class IAudioStt {
 public:
  virtual ~IAudioStt() = default;
  virtual void setAudioDesc(AudioDesc desc) = 0;
  // 开始识别任务 (内部按需加载/复用模型); 替代旧 load()
  virtual void start() = 0;
  virtual void recognize(const AvoxData& adata, int64_t pts) = 0;
  // 停止识别任务 (join 排空剩余音频, 模型常驻); 替代旧 unload()
  virtual void stop() = 0;
  virtual bool loading() = 0;
  virtual void setModelLevel(ModelLevel level) = 0;
  virtual void setRecognizerType(RecognizerType type) = 0;
  virtual RecognizerType getRecognizerType() = 0;
};

// ============== 语音合成 ==============

// 合成器后端类型(创建实例用,内部映射到工厂表字符串 key)
enum class AudioTtsType { none, sherpa };

// 语音合成回调接口
class IAudioTtsOb {
 public:
  virtual ~IAudioTtsOb() = default;
  // 合成开始时回调一次输出格式(随后开始 onTtsAudio)
  virtual void onTtsDesc(const AudioDesc& desc) {}
  // 定长 PCM 块就绪(已重采样/切片到 setAudioDesc 指定格式);
  // final=true 表示本次 synthesize 的最后一块;raw 仅回调内有效
  virtual void onTtsAudio(const AvoxData& raw, int64_t pts, bool final) {}
  // 合成失败
  virtual void onTtsError(const char* err) {}
};

// 语音合成器接口(对外导出,其他语言通过此接口使用 TTS)
// 方向与 IAudioStt 相反:文本进(synthesize)→ PCM 出(onTtsAudio)
class IAudioTts {
 public:
  virtual ~IAudioTts() = default;
  // 期望输出格式(sampleRate=0 跟随模型原生);合成前设置
  virtual void setAudioDesc(AudioDesc desc) = 0;
  // 开始合成任务 (内部按需加载/复用模型)
  virtual void start() = 0;
  // 合成一段文本(可句级重复调用实现流式);PCM 经 IAudioTtsOb 回调
  virtual void synthesize(const char* text) = 0;
  // 中断当前合成 (barge-in 打断), 模型常驻
  virtual void stop() = 0;
  virtual bool loading() = 0;
  virtual void setModelLevel(ModelLevel level) = 0;
  // 语速 (1.0 正常), 由 [SPEED:x] 标记驱动
  virtual void setSpeed(float speed) = 0;
  // 说话人 ID (多说话人模型用, 单说话人忽略)
  virtual void setSpeaker(int sid) = 0;
};

// ============== 语音→面部 blendshape (虚拟人域) ==============
// avatar 域(audio 驱动): PCM 进(feed, 来自 TTS/麦克风 tap) → ARKit52 blendshape 出,
// 驱动虚拟人口型/表情。实现由 avox_avatar 插件(wav2arkit 等)提供, 接口可替换。
// 后续非音频 avatar 接口(骨骼/表情驱动)另建 AvoxAvatar.h, 留待扩展。

// blendshape 流描述(onFaceDesc 回调一次)
struct FaceDesc {
  int32_t fps = 30;              // 输出帧率 (wav2arkit 固定 30fps)
  int32_t blendshapeCount = 52;  // ARKit52
};

// 后端类型(创建实例用, 内部映射工厂表字符串 key)
enum class AudioFaceType { none, wav2arkit };

// blendshape 回调接口
class IAudioFaceOb {
 public:
  virtual ~IAudioFaceOb() = default;
  // 模型就绪后回调一次输出描述(随后开始 onFaceBlendshape)
  virtual void onFaceDesc(const FaceDesc& desc) {}
  // 单帧 blendshape 就绪: raw52 = blendshapeCount×float (值域[0,1]), raw 仅回调内有效;
  // pts = 该帧时间戳(ms); final=true 表示本次 feed 触发推理的最后帧
  virtual void onFaceBlendshape(const AvoxData& raw52, int64_t pts, bool final) {}
  // 推理失败
  virtual void onFaceError(const char* err) {}
};

// 音频→blendshape 接口(对外导出)
// 方向: PCM 进(feed) → ARKit52 blendshape 出(经 IAudioFaceOb::onFaceBlendshape)
class IAudioFace {
 public:
  virtual ~IAudioFace() = default;
  // 输入 PCM 格式(任意采样率/格式, 内部重采样到模型期望 16kHz mono float32); feed 前设置
  virtual void setAudioDesc(AudioDesc desc) = 0;
  // 开始推理任务 (内部按需加载/复用模型)
  virtual void start() = 0;
  // 喂一段 PCM(可流式重复调用);blendshape 经 IAudioFaceOb 回调
  virtual void feed(const AvoxData& pcm, int64_t pts) = 0;
  // 停止任务 (flush 剩余 + join worker, 模型常驻)
  virtual void stop() = 0;
  virtual bool loading() = 0;
  virtual void setModelLevel(ModelLevel level) = 0;
};

extern "C" {
AVOX_EXPORT int32_t audioFormatSize(AudioFormat format);
AVOX_EXPORT int32_t getSamples(int32_t size, const AudioDesc& desc);
AVOX_EXPORT int32_t getAudioFrameMs(const AudioDesc& audioDesc, int32_t size);
AVOX_EXPORT const char* getAudioFormatStr(AudioFormat format);
AVOX_EXPORT int32_t getAudioFrameSize(const AudioDesc& audioDesc,
                                     int32_t frameMs);
AVOX_EXPORT bool bAPlaneFormat(AudioFormat format);
AVOX_EXPORT AudioFormat nPlaneFormat(AudioFormat format);
AVOX_EXPORT bool bAdtsHeader(const uint8_t* data, int32_t size);
AVOX_EXPORT void adtsHeader(uint8_t* data, int32_t size, uint8_t sampleRateIndex,
                           int32_t channelCount, int32_t profile = 1);
AVOX_EXPORT void addAdtsHeader(const AudioDesc& audioDesc, uint8_t* data,
                              int32_t size, int32_t profile = 1);
AVOX_EXPORT void ascHeader(const AudioDesc& audioDesc, uint8_t* data,
                          int32_t profile = 2);
AVOX_EXPORT int32_t getSampleRateByIndex(uint8_t index);
AVOX_EXPORT void addAudioTapOb(IAudioRender* r, IAudioTapOb* ob);
AVOX_EXPORT void removeAudioTapOb(IAudioRender* r, IAudioTapOb* ob);
AVOX_EXPORT IWavSave* createWavSave();
// 创建语音识别器(后端未注册/none 返回 nullptr)
AVOX_EXPORT IAudioStt* createAudioStt(AudioSttType type);

AVOX_EXPORT void addAudioSttOb(IAudioStt* stt, IAudioSttOb* ob);
AVOX_EXPORT void removeAudioSttOb(IAudioStt* stt, IAudioSttOb* ob);

// 创建语音合成器(后端未注册/none 返回 nullptr)
AVOX_EXPORT IAudioTts* createAudioTts(AudioTtsType type);

AVOX_EXPORT void addAudioTtsOb(IAudioTts* tts, IAudioTtsOb* ob);
AVOX_EXPORT void removeAudioTtsOb(IAudioTts* tts, IAudioTtsOb* ob);

// 创建音频→blendshape 推理器(后端未注册/none 返回 nullptr)
AVOX_EXPORT IAudioFace* createAudioFace(AudioFaceType type);
AVOX_EXPORT void addAudioFaceOb(IAudioFace* face, IAudioFaceOb* ob);
AVOX_EXPORT void removeAudioFaceOb(IAudioFace* face, IAudioFaceOb* ob);
}

}
