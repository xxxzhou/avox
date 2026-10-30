#pragma once

#include "AvoxDef.h"
#include "AvoxImage.h"
#include "AvoxSource.h"

namespace avox {

// 媒体读取器操作阶段(值导出, 只增不改)
#define AVOX_MAP_MEDIA_OP(XX) \
  XX(open, 0, "open")         \
  XX(index, 1, "index")       \
  XX(frame, 2, "frame")       \
  XX(close, 3, "close")

enum class MediaOp : int32_t {
#define XX(name, value, str) name = value,
  AVOX_MAP_MEDIA_OP(XX)
#undef XX
};

// reader 状态机(值导出, 只增不改); 迁移表见 avox_ffmpeg/MediaReaderUtil.hpp
#define AVOX_MAP_MEDIA_READER_STATE(XX) \
  XX(idle, 0, "idle")                   \
  XX(opening, 1, "opening")             \
  XX(ready, 2, "ready")                 \
  XX(indexing, 3, "indexing")           \
  XX(grabbing, 4, "grabbing")           \
  XX(closing, 5, "closing")

enum class MediaReaderState : int32_t {
#define XX(name, value, str) name = value,
  AVOX_MAP_MEDIA_READER_STATE(XX)
#undef XX
};

// 错误码(值导出, 只增不改; 值连续=字符串表下标, 追加只能往后排)
#define AVOX_MAP_MEDIA_ERROR(XX)                        \
  XX(none, 0, "none")                                   \
  XX(inputOpenFailed, 1, "input open failed")           \
  XX(findStreamInfoFailed, 2, "stream info failed")     \
  XX(noVideo, 3, "no video")                            \
  XX(decoderMissing, 4, "decoder missing")              \
  XX(seekFailed, 5, "seek failed")                      \
  XX(decodeFailed, 6, "decode failed")                  \
  XX(unsupported, 7, "unsupported")                     \
  XX(unsupportedPixFmt, 8, "unsupported pix fmt")       \
  XX(renderUnavailable, 9, "render unavailable")        \
  XX(cancelled, 10, "cancelled")                        \
  XX(invalidState, 11, "invalid state")                 \
  XX(invalidArgument, 12, "invalid argument")           \
  XX(timeout, 13, "timeout")                            \
  XX(other, 14, "other")

enum class MediaError : int32_t {
#define XX(name, value, str) name = value,
  AVOX_MAP_MEDIA_ERROR(XX)
#undef XX
};

// 一次索引请求(轨=容器 stream index; -1=best stream, 与 ak_index 同口径)
struct MediaIndexOptions {
  // 音频峰值桶数, 实际按 [64,4000] 钳位
  int32_t buckets = 1200;
  int32_t audioTrack = -1;
  int32_t videoTrack = -1;
};

// 索引结果(peaks/keyframesMs 只在 onIndexDone 回调内有效, 需留存当场拷贝)
struct MediaIndexResult {
  int32_t structSize = sizeof(MediaIndexResult);
  // 有效时长: 容器时长优先, 否则 stream duration, 再否则音频末帧推算
  int64_t durationMs = 0;
  int32_t bucketMs = 0;
  // 选中音频轨存在(与解码器是否可用无关)
  bool hasAudio = false;
  // 音频解码器成功打开(打不开时 peaks 为空)
  bool waveAvailable = false;
  // 桶内线性幅度最大值 [0,1]; shim 转 sqrt(p)*100 保持 0-100 UI
  const float* peaks = nullptr;
  int32_t peakCount = 0;
  // 关键帧毫秒时间戳, 单调递增
  const int64_t* keyframesMs = nullptr;
  int32_t keyframeCount = 0;
};

// 一次抓帧请求
struct MediaFrameRequest {
  // 目标时间(ms); <0 按 0, 超出片长按片尾
  int64_t timeMs = 0;
  // 期望输出宽度 [48,1280], 只是请求/分配提示; 实际输出尺寸以 onFrame 的
  // image->getImageFormat() 为准(图像处理链可能改变尺寸)
  int32_t width = 320;
  // 容器 stream index; -1 = 第一条非封面视频轨
  int32_t videoTrack = -1;
};

// 抓帧结果(width/height=实际输出尺寸, 回调时从 image->getImageFormat() 填入;
// 不是 open/probe 预算值)
struct MediaFrameInfo {
  int32_t structSize = sizeof(MediaFrameInfo);
  int64_t requestedMs = 0;
  // 实际解码帧时间, 可能晚于请求(从关键帧向后解)
  int64_t actualMs = 0;
  int32_t width = 0;
  int32_t height = 0;
};

// reader 事件观察者; 所有回调串行不并发, 回调内只能调 cancel(),
// 不得重入 open/buildIndex/grabFrame/close
class IMediaReaderOb {
 public:
  virtual ~IMediaReaderOb() = default;

 public:
  virtual void onStateChange(MediaReaderState pre, MediaReaderState state) {}
  // open 成功; ISourceInfo 从这一刻起有效
  virtual void onReady() {}
  // buildIndex 进度 0-99; onIndexDone 即完成
  virtual void onIndexProgress(int32_t percent) {}
  virtual void onIndexDone(const MediaIndexResult& result) {}
  // grabFrame 完成; image 只在回调内有效, 需留存当场拷贝/编码
  virtual void onFrame(const MediaFrameInfo& info, IImageBuffer* image) {}
  // op 指明失败阶段; 回调后状态回退
  virtual void onError(MediaOp op, MediaError code, const char* message) {}
  virtual void onClose() {}
};

// 只读媒体读取器: 一次 open, 探测/索引/抓帧复用同一个容器上下文。
// 结果只经 observer 回调; 返回值只表示请求是否被接受(不接受则不触发回调)。
// open/buildIndex/grabFrame/close 由调用方同线程串行调用; cancel() 可跨线程
class IMediaReader {
 public:
  virtual ~IMediaReader() = default;

 public:
  // 必须在 open 前设置; observer 生命周期覆盖到 close 返回
  virtual void setObserver(IMediaReaderOb* ob) = 0;
  // 返回 false = 请求未被接受(状态不对/参数非法), 读 lastError
  virtual bool open(const char* uri) = 0;
  virtual bool buildIndex(const MediaIndexOptions& options) = 0;
  // 抓帧走 Vulkan 离屏渲染(方向由渲染层保证); 无 Vulkan 构建回
  // onError(frame, renderUnavailable), 不做 CPU 兜底
  virtual bool grabFrame(const MediaFrameRequest& request) = 0;
  // 线程安全: 置取消标志打断当前 open/index/frame, 经 onError(op,cancelled) 收尾
  virtual void cancel() = 0;
  // 同步关闭; idle 时为 no-op; close 后回 idle 可重新 open
  virtual void close() = 0;
  // onReady 后到 close 前有效, reader 托管
  virtual ISourceInfo* getSourceInfo() = 0;
  virtual MediaError lastError() = 0;
  // reader 托管, 下一次操作/close 前有效
  virtual const char* lastErrorMessage() = 0;
};

extern "C" {
// 工厂(需释放); 无 FFmpeg 构建返回 nullptr
AVOX_EXPORT IMediaReader* createMediaReader();
AVOX_EXPORT const char* getMediaReaderStateStr(MediaReaderState state);
AVOX_EXPORT const char* getMediaOpStr(MediaOp op);
AVOX_EXPORT const char* getMediaErrorStr(MediaError error);
}

}  // namespace avox
