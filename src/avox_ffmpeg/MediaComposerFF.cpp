#include "avox/AvoxCapture.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <fstream>
#include <new>
#include <string>
#include <vector>

#include "avox/module/LogHelper.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
}

namespace avox {

namespace {

std::string avError(int code) {
  char buf[AV_ERROR_MAX_STRING_SIZE] = {};
  av_strerror(code, buf, sizeof(buf));
  return buf;
}

uint16_t readU16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }

uint32_t readU32(const uint8_t* p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}

// 录屏 WAV 是固定 44 字节头(PCM/IEEE float, 无 extensible), 但按 chunk 遍历更稳。
// 自解析的原因: 出厂 FFmpeg 白名单不保证带 pcm_f32le, 而 WASAPI 回环常落 float32。
class WavReader {
 public:
  WavReader() = default;
  ~WavReader() { close(); }
  WavReader(const WavReader&) = delete;
  WavReader& operator=(const WavReader&) = delete;
  WavReader(WavReader&& other) noexcept { moveFrom(other); }
  WavReader& operator=(WavReader&& other) noexcept {
    if (this != &other) {
      close();
      moveFrom(other);
    }
    return *this;
  }

  bool open(const std::string& path, std::string* error) {
    close();
    file.open(path, std::ios::binary);
    if (!file.is_open()) {
      *error = "cannot open wav: " + path;
      return false;
    }
    uint8_t hdr[12] = {};
    file.read(reinterpret_cast<char*>(hdr), sizeof(hdr));
    if (file.gcount() != sizeof(hdr) || std::memcmp(hdr, "RIFF", 4) != 0 ||
        std::memcmp(hdr + 8, "WAVE", 4) != 0) {
      *error = "not a RIFF/WAVE file: " + path;
      return false;
    }
    bool haveFmt = false;
    bool haveData = false;
    while (file.good()) {
      uint8_t chunk[8] = {};
      file.read(reinterpret_cast<char*>(chunk), sizeof(chunk));
      if (file.gcount() != sizeof(chunk)) {
        break;
      }
      const uint32_t size = readU32(chunk + 4);
      const std::streamoff next = file.tellg() + std::streamoff(size);
      if (std::memcmp(chunk, "fmt ", 4) == 0) {
        std::vector<uint8_t> fmt(size < 64 ? size : 64);
        file.read(reinterpret_cast<char*>(fmt.data()), fmt.size());
        if (fmt.size() >= 16) {
          formatTag = readU16(fmt.data());
          channels = readU16(fmt.data() + 2);
          sampleRate = readU32(fmt.data() + 4);
          blockAlign = readU16(fmt.data() + 12);
          bits = readU16(fmt.data() + 14);
          if (formatTag == 0xFFFE && fmt.size() >= 40) {
            // WAVE_FORMAT_EXTENSIBLE: SubFormat GUID 前两字节是真实 tag
            formatTag = readU16(fmt.data() + 24);
          }
          haveFmt = channels > 0 && sampleRate > 0 && blockAlign > 0;
        }
      } else if (std::memcmp(chunk, "data", 4) == 0) {
        dataOffset = file.tellg();
        dataSize = size;
        file.seekg(0, std::ios::end);
        const std::streamoff end = file.tellg();
        file.seekg(dataOffset);
        const int64_t avail = (int64_t)(end - dataOffset);
        if (dataSize > avail) {
          dataSize = (uint32_t)std::max<int64_t>(0, avail);
        }
        haveData = true;
        if (haveFmt) {
          break;
        }
      }
      file.seekg(next);
    }
    if (!haveFmt || !haveData || blockAlign == 0) {
      *error = "wav missing fmt/data chunk: " + path;
      return false;
    }
    if (formatTag != 1 && formatTag != 3) {
      *error = "unsupported wav format tag: " + std::to_string(formatTag);
      return false;
    }
    totalFrames = (int64_t)dataSize / blockAlign;
    pos = 0;
    return true;
  }

  void close() {
    if (file.is_open()) {
      file.close();
    }
    dataOffset = 0;
    dataSize = 0;
    totalFrames = 0;
    pos = 0;
  }

  int getChannels() const { return channels; }
  int getSampleRate() const { return (int)sampleRate; }
  int getBits() const { return bits; }
  int getFormatTag() const { return formatTag; }
  int64_t getTotalFrames() const { return totalFrames; }

  // 读最多 frames 帧, 转成 float 交错; 返回实际帧数
  int readFrames(float* dst, int frames) {
    if (!file.is_open() || frames <= 0 || pos >= totalFrames) {
      return 0;
    }
    const int want = (int)std::min<int64_t>(frames, totalFrames - pos);
    const size_t bytes = (size_t)want * blockAlign;
    raw.resize(bytes);
    file.read(reinterpret_cast<char*>(raw.data()), bytes);
    const int got = (int)(file.gcount() / blockAlign);
    if (got <= 0) {
      return 0;
    }
    pos += got;
    const int ch = channels;
    const uint8_t* src = raw.data();
    if (formatTag == 3) {
      if (bits == 32) {
        std::memcpy(dst, src, (size_t)got * ch * sizeof(float));
      } else if (bits == 64) {
        const double* d = reinterpret_cast<const double*>(src);
        for (int i = 0; i < got * ch; i++) {
          dst[i] = (float)d[i];
        }
      } else {
        return 0;
      }
      return got;
    }
    if (bits == 8) {
      for (int i = 0; i < got * ch; i++) {
        dst[i] = ((int)src[i] - 128) / 128.0f;
      }
    } else if (bits == 16) {
      const int16_t* s = reinterpret_cast<const int16_t*>(src);
      for (int i = 0; i < got * ch; i++) {
        dst[i] = s[i] / 32768.0f;
      }
    } else if (bits == 24) {
      for (int i = 0; i < got * ch; i++) {
        const uint8_t* p = src + i * 3;
        int32_t v = (int32_t)((uint32_t)p[0] << 8 | (uint32_t)p[1] << 16 |
                              (uint32_t)p[2] << 24) >> 8;
        dst[i] = v / 8388608.0f;
      }
    } else if (bits == 32) {
      const int32_t* s = reinterpret_cast<const int32_t*>(src);
      for (int i = 0; i < got * ch; i++) {
        dst[i] = (float)(s[i] / 2147483648.0);
      }
    } else {
      return 0;
    }
    return got;
  }

 private:
  void moveFrom(WavReader& other) {
    file = std::move(other.file);
    raw = std::move(other.raw);
    formatTag = other.formatTag;
    channels = other.channels;
    sampleRate = other.sampleRate;
    blockAlign = other.blockAlign;
    bits = other.bits;
    dataOffset = other.dataOffset;
    dataSize = other.dataSize;
    totalFrames = other.totalFrames;
    pos = other.pos;
    other.formatTag = 0;
    other.channels = 0;
    other.sampleRate = 0;
    other.blockAlign = 0;
    other.bits = 0;
    other.dataOffset = 0;
    other.dataSize = 0;
    other.totalFrames = 0;
    other.pos = 0;
  }

  std::ifstream file;
  std::vector<uint8_t> raw;
  uint16_t formatTag = 0;
  uint16_t channels = 0;
  uint32_t sampleRate = 0;
  uint16_t blockAlign = 0;
  uint16_t bits = 0;
  std::streamoff dataOffset = 0;
  uint32_t dataSize = 0;
  int64_t totalFrames = 0;
  int64_t pos = 0;
};

struct TrackState {
  std::string path;
  WavReader reader;
  SwrContext* swr = nullptr;
  AVChannelLayout inLayout = {};
  AVChannelLayout outLayout = {};
  std::deque<float> left;
  std::deque<float> right;
  int64_t firstIndex = 0;  // left.front() 对应的输出帧号
  double gain = 1.0;
  bool eof = false;
  bool flushed = false;
};

bool ensureTrack(TrackState* tr, size_t need) {
  if (!tr) {
    return false;
  }
  const int ch = tr->reader.getChannels();
  std::vector<float> in;
  std::vector<float> outL;
  std::vector<float> outR;
  while (tr->left.size() < need && !tr->eof) {
    const int want = 2048;
    in.resize((size_t)want * ch);
    const int got = tr->reader.readFrames(in.data(), want);
    if (got <= 0) {
      tr->eof = true;
      break;
    }
    const int outCap = (int)swr_get_out_samples(tr->swr, got);
    if (outCap <= 0) {
      continue;
    }
    outL.resize(outCap);
    outR.resize(outCap);
    uint8_t* out[2] = {reinterpret_cast<uint8_t*>(outL.data()),
                       reinterpret_cast<uint8_t*>(outR.data())};
    const uint8_t* src[1] = {reinterpret_cast<const uint8_t*>(in.data())};
    const int converted = swr_convert(tr->swr, out, outCap, src, got);
    for (int i = 0; i < converted; i++) {
      tr->left.push_back(outL[i]);
      tr->right.push_back(outR[i]);
    }
  }
  if (tr->eof && !tr->flushed) {
    tr->flushed = true;
    // 刷新重采样器尾部
    const int outCap = (int)swr_get_out_samples(tr->swr, 0);
    if (outCap > 0) {
      outL.resize(outCap);
      outR.resize(outCap);
      uint8_t* out[2] = {reinterpret_cast<uint8_t*>(outL.data()),
                         reinterpret_cast<uint8_t*>(outR.data())};
      const int converted = swr_convert(tr->swr, out, outCap, nullptr, 0);
      for (int i = 0; i < converted; i++) {
        tr->left.push_back(outL[i]);
        tr->right.push_back(outR[i]);
      }
    }
  }
  return true;
}

void dropConsumed(TrackState* tr, int64_t chunkEnd) {
  const int64_t available = (int64_t)tr->left.size();
  const int64_t drop = std::min(available, chunkEnd - tr->firstIndex);
  if (drop <= 0) {
    return;
  }
  for (int64_t i = 0; i < drop; i++) {
    tr->left.pop_front();
    tr->right.pop_front();
  }
  tr->firstIndex += drop;
}

bool flushEncoder(AVCodecContext* ctx, AVFormatContext* fmt, AVStream* st,
                  std::string* error) {
  int ret = avcodec_send_frame(ctx, nullptr);
  if (ret < 0 && ret != AVERROR_EOF) {
    *error = "aac flush failed: " + avError(ret);
    return false;
  }
  AVPacket* pkt = av_packet_alloc();
  while (ret >= 0) {
    ret = avcodec_receive_packet(ctx, pkt);
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
      break;
    }
    if (ret < 0) {
      *error = "aac receive failed: " + avError(ret);
      av_packet_free(&pkt);
      return false;
    }
    av_packet_rescale_ts(pkt, ctx->time_base, st->time_base);
    pkt->stream_index = st->index;
    if (av_interleaved_write_frame(fmt, pkt) < 0) {
      *error = "aac write failed";
      av_packet_free(&pkt);
      return false;
    }
    av_packet_unref(pkt);
  }
  av_packet_free(&pkt);
  return true;
}

struct PacketCursor {
  AVFormatContext* fmt = nullptr;
  int stream = -1;
  AVPacket* packet = nullptr;

  ~PacketCursor() { av_packet_free(&packet); }
};

bool readNext(PacketCursor* cur) {
  if (!cur || !cur->fmt || !cur->packet) {
    return false;
  }
  while (true) {
    const int ret = av_read_frame(cur->fmt, cur->packet);
    if (ret < 0) {
      return false;
    }
    if (cur->packet->stream_index == cur->stream) {
      return true;
    }
    av_packet_unref(cur->packet);
  }
}

int64_t packetTs(const AVPacket* pkt) {
  return pkt->dts != AV_NOPTS_VALUE ? pkt->dts : pkt->pts;
}

// 按输出后缀建 muxer。自定义 FFmpeg 构建可能缩减扩展名表(实测有构建不认
// .m4a/.3gp, 内核仍是同一个 mov muxer), 音频临时文件允许退回按 mp4 显式建。
bool allocOutputContext(AVFormatContext** ofmt, const std::string& path,
                        bool allowMp4Fallback, std::string* error) {
  *ofmt = nullptr;
  int ret = avformat_alloc_output_context2(ofmt, nullptr, nullptr, path.c_str());
  if (ret >= 0 && *ofmt) {
    return true;
  }
  if (allowMp4Fallback) {
    *ofmt = nullptr;
    ret = avformat_alloc_output_context2(ofmt, nullptr, "mp4", path.c_str());
    if (ret >= 0 && *ofmt) {
      return true;
    }
  }
  *error = "output muxer unavailable for: " + path;
  return false;
}

}  // namespace

class MediaComposerFF : public IMediaComposer {
 public:
  MediaComposerFF() = default;
  virtual ~MediaComposerFF() = default;

  virtual void reset() override {
    tracks.clear();
    errorText.clear();
  }
  virtual void addAudioTrack(const char* wavPath, int64_t startMs, float gainDb,
                             bool mute) override {
    TrackSpec spec;
    spec.path = wavPath ? wavPath : "";
    spec.startMs = startMs;
    spec.gainDb = gainDb;
    spec.mute = mute;
    tracks.push_back(std::move(spec));
  }
  virtual MediaMixStatus mixAudio(int64_t videoPts0Ms, int64_t startMs,
                                  int64_t endMs, const char* outPath) override;
  virtual bool remux(const char* videoPath, const char* audioPath,
                     const char* outPath) override;
  virtual const char* getLastError() override { return errorText.c_str(); }

 private:
  struct TrackSpec {
    std::string path;
    int64_t startMs = -1;
    float gainDb = 0.0f;
    bool mute = false;
  };

 private:
  std::vector<TrackSpec> tracks;
  std::string errorText;
};

MediaMixStatus MediaComposerFF::mixAudio(int64_t videoPts0Ms, int64_t startMs,
                                         int64_t endMs, const char* outPath) {
  errorText.clear();
  const std::string out = outPath ? outPath : "";
  if (out.empty()) {
    errorText = "audio output path is empty";
    return MediaMixStatus::failed;
  }
  if (endMs <= startMs) {
    return MediaMixStatus::noAudio;
  }
  const int64_t durationMs = endMs - startMs;
  const int outRate = 48000;
  const int64_t totalFrames = durationMs * outRate / 1000;
  if (totalFrames <= 0) {
    return MediaMixStatus::noAudio;
  }

  std::vector<TrackState> states;
  for (const auto& spec : tracks) {
    if (spec.mute || spec.startMs < 0 || spec.path.empty()) {
      continue;
    }
    TrackState tr;
    tr.path = spec.path;
    tr.gain = std::pow(10.0, spec.gainDb / 20.0);
    std::string error;
    if (!tr.reader.open(spec.path, &error)) {
      // 单轨坏不拖垮整次混音
      continue;
    }
    av_channel_layout_default(&tr.inLayout, tr.reader.getChannels());
    av_channel_layout_default(&tr.outLayout, 2);
    const int ret = swr_alloc_set_opts2(
        &tr.swr, &tr.outLayout, AV_SAMPLE_FMT_FLTP, outRate, &tr.inLayout,
        AV_SAMPLE_FMT_FLT, tr.reader.getSampleRate(), 0, nullptr);
    if (ret < 0 || !tr.swr || swr_init(tr.swr) < 0) {
      if (tr.swr) {
        swr_free(&tr.swr);
      }
      continue;
    }
    const int64_t trackStart = spec.startMs - videoPts0Ms;
    const int64_t offsetMs = trackStart - startMs;
    if (offsetMs >= durationMs) {
      swr_free(&tr.swr);
      continue;
    }
    const int64_t startFrame = offsetMs > 0 ? offsetMs * outRate / 1000 : 0;
    const int64_t skipFrames = offsetMs < 0 ? (-offsetMs) * outRate / 1000 : 0;
    // 队列下标 0 = 该轨第 0 个样本; firstIndex<0 时前 |firstIndex| 个样本
    // 落在导出区间之前, 混音时按 n-firstIndex 跳过
    tr.firstIndex = startFrame - skipFrames;
    states.push_back(std::move(tr));
  }
  if (states.empty()) {
    return MediaMixStatus::noAudio;
  }

  const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_AAC);
  if (!codec) {
    errorText = "aac encoder unavailable";
    return MediaMixStatus::failed;
  }
  AVCodecContext* ctx = avcodec_alloc_context3(codec);
  if (!ctx) {
    errorText = "aac context alloc failed";
    return MediaMixStatus::failed;
  }
  ctx->sample_fmt = AV_SAMPLE_FMT_FLTP;
  ctx->sample_rate = outRate;
  av_channel_layout_default(&ctx->ch_layout, 2);
  ctx->bit_rate = 192000;
  ctx->time_base = {1, outRate};
  int ret = avcodec_open2(ctx, codec, nullptr);
  if (ret < 0) {
    errorText = "aac open failed: " + avError(ret);
    avcodec_free_context(&ctx);
    return MediaMixStatus::failed;
  }
  const int frameSize = ctx->frame_size > 0 ? ctx->frame_size : 1024;

  AVFormatContext* ofmt = nullptr;
  if (!allocOutputContext(&ofmt, out, true, &errorText)) {
    avcodec_free_context(&ctx);
    return MediaMixStatus::failed;
  }
  AVStream* ost = avformat_new_stream(ofmt, nullptr);
  avcodec_parameters_from_context(ost->codecpar, ctx);
  ost->time_base = ctx->time_base;
  if (avio_open(&ofmt->pb, out.c_str(), AVIO_FLAG_WRITE) < 0) {
    errorText = "audio output open failed: " + out;
    avformat_free_context(ofmt);
    avcodec_free_context(&ctx);
    return MediaMixStatus::failed;
  }
  ret = avformat_write_header(ofmt, nullptr);
  if (ret < 0) {
    errorText = "audio write header failed: " + avError(ret);
    avio_closep(&ofmt->pb);
    avformat_free_context(ofmt);
    avcodec_free_context(&ctx);
    return MediaMixStatus::failed;
  }

  AVFrame* frame = av_frame_alloc();
  frame->nb_samples = frameSize;
  frame->format = AV_SAMPLE_FMT_FLTP;
  frame->sample_rate = outRate;
  av_channel_layout_copy(&frame->ch_layout, &ctx->ch_layout);
  av_frame_get_buffer(frame, 0);

  std::vector<float> mixL(frameSize);
  std::vector<float> mixR(frameSize);
  AVPacket* pkt = av_packet_alloc();
  int64_t produced = 0;
  bool writeError = false;
  while (produced < totalFrames) {
    const int chunk = frameSize;
    std::fill(mixL.begin(), mixL.end(), 0.0f);
    std::fill(mixR.begin(), mixR.end(), 0.0f);
    const int64_t chunkStart = produced;
    const int64_t chunkEnd = produced + chunk;
    for (auto& tr : states) {
      const int64_t need = chunkEnd - tr.firstIndex + 1;
      if (need > 0 && (int64_t)tr.left.size() < need) {
        ensureTrack(&tr, (size_t)need);
      }
      const int64_t from = std::max(chunkStart, tr.firstIndex);
      const int64_t to =
          std::min(chunkEnd, tr.firstIndex + (int64_t)tr.left.size());
      for (int64_t n = from; n < to; n++) {
        const size_t idx = (size_t)(n - tr.firstIndex);
        const size_t dst = (size_t)(n - chunkStart);
        mixL[dst] += (float)(tr.left[idx] * tr.gain);
        mixR[dst] += (float)(tr.right[idx] * tr.gain);
      }
      dropConsumed(&tr, chunkEnd);
    }
    float* l = reinterpret_cast<float*>(frame->data[0]);
    float* r = reinterpret_cast<float*>(frame->data[1]);
    for (int i = 0; i < chunk; i++) {
      l[i] = std::max(-1.0f, std::min(1.0f, mixL[i]));
      r[i] = std::max(-1.0f, std::min(1.0f, mixR[i]));
    }
    frame->pts = produced;
    ret = avcodec_send_frame(ctx, frame);
    if (ret < 0) {
      writeError = true;
      errorText = "aac encode failed: " + avError(ret);
      break;
    }
    while ((ret = avcodec_receive_packet(ctx, pkt)) >= 0) {
      av_packet_rescale_ts(pkt, ctx->time_base, ost->time_base);
      pkt->stream_index = ost->index;
      if (av_interleaved_write_frame(ofmt, pkt) < 0) {
        writeError = true;
        errorText = "audio mux write failed";
        break;
      }
      av_packet_unref(pkt);
    }
    if (writeError) {
      break;
    }
    produced += chunk;
  }
  if (!writeError) {
    flushEncoder(ctx, ofmt, ost, &errorText);
  }
  av_packet_free(&pkt);
  av_frame_free(&frame);
  av_write_trailer(ofmt);
  avio_closep(&ofmt->pb);
  avformat_free_context(ofmt);
  avcodec_free_context(&ctx);
  for (auto& tr : states) {
    if (tr.swr) {
      swr_free(&tr.swr);
    }
  }
  if (!errorText.empty()) {
    std::remove(out.c_str());
    return MediaMixStatus::failed;
  }
  return MediaMixStatus::ok;
}

bool MediaComposerFF::remux(const char* videoPath, const char* audioPath,
                            const char* outPath) {
  errorText.clear();
  const std::string video = videoPath ? videoPath : "";
  const std::string audio = audioPath ? audioPath : "";
  const std::string out = outPath ? outPath : "";
  if (video.empty() || out.empty()) {
    errorText = "remux requires video and output path";
    return false;
  }
  AVFormatContext* vfmt = nullptr;
  AVFormatContext* afmt = nullptr;
  AVFormatContext* ofmt = nullptr;
  int ret = avformat_open_input(&vfmt, video.c_str(), nullptr, nullptr);
  if (ret < 0) {
    errorText = "open video failed: " + avError(ret);
    return false;
  }
  avformat_find_stream_info(vfmt, nullptr);
  const int vindex =
      av_find_best_stream(vfmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
  if (vindex < 0) {
    errorText = "video stream missing";
    avformat_close_input(&vfmt);
    return false;
  }
  int aindex = -1;
  if (!audio.empty()) {
    ret = avformat_open_input(&afmt, audio.c_str(), nullptr, nullptr);
    if (ret >= 0) {
      avformat_find_stream_info(afmt, nullptr);
      aindex =
          av_find_best_stream(afmt, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    }
    if (aindex < 0) {
      avformat_close_input(&afmt);
      afmt = nullptr;
    }
  }
  if (!allocOutputContext(&ofmt, out, false, &errorText)) {
    avformat_close_input(&vfmt);
    if (afmt) {
      avformat_close_input(&afmt);
    }
    return false;
  }
  AVStream* vin = vfmt->streams[vindex];
  AVStream* vout = avformat_new_stream(ofmt, nullptr);
  avcodec_parameters_copy(vout->codecpar, vin->codecpar);
  vout->codecpar->codec_tag = 0;
  if (vin->codecpar->codec_id == AV_CODEC_ID_HEVC && ofmt->oformat->name &&
      std::string(ofmt->oformat->name).find("mp4") != std::string::npos) {
    vout->codecpar->codec_tag = MKTAG('h', 'v', 'c', '1');
  }
  vout->time_base = vin->time_base;
  AVStream* ain = nullptr;
  AVStream* aout = nullptr;
  if (afmt && aindex >= 0) {
    ain = afmt->streams[aindex];
    aout = avformat_new_stream(ofmt, nullptr);
    avcodec_parameters_copy(aout->codecpar, ain->codecpar);
    aout->codecpar->codec_tag = 0;
    aout->time_base = ain->time_base;
  }
  const bool isMp4 =
      ofmt->oformat->name && (std::string(ofmt->oformat->name) == "mp4" ||
                              std::string(ofmt->oformat->name) == "mov");
  if (isMp4) {
    av_opt_set(ofmt->priv_data, "movflags", "+faststart", 0);
  }
  if (avio_open(&ofmt->pb, out.c_str(), AVIO_FLAG_WRITE) < 0) {
    errorText = "output open failed: " + out;
    avformat_free_context(ofmt);
    avformat_close_input(&vfmt);
    if (afmt) {
      avformat_close_input(&afmt);
    }
    return false;
  }
  ret = avformat_write_header(ofmt, nullptr);
  if (ret < 0) {
    errorText = "write header failed: " + avError(ret);
    avio_closep(&ofmt->pb);
    avformat_free_context(ofmt);
    avformat_close_input(&vfmt);
    if (afmt) {
      avformat_close_input(&afmt);
    }
    return false;
  }

  PacketCursor vc;
  vc.fmt = vfmt;
  vc.stream = vindex;
  vc.packet = av_packet_alloc();
  PacketCursor ac;
  if (ain) {
    ac.fmt = afmt;
    ac.stream = aindex;
    ac.packet = av_packet_alloc();
  }
  bool vhas = readNext(&vc);
  bool ahas = ac.fmt && readNext(&ac);
  while (vhas || ahas) {
    bool writeVideo = false;
    if (vhas && ahas) {
      writeVideo = av_compare_ts(packetTs(vc.packet), vin->time_base,
                                 packetTs(ac.packet), ain->time_base) <= 0;
    } else {
      writeVideo = vhas;
    }
    PacketCursor* cur = writeVideo ? &vc : &ac;
    AVStream* inSt = writeVideo ? vin : ain;
    AVStream* outSt = writeVideo ? vout : aout;
    AVPacket* pkt = cur->packet;
    av_packet_rescale_ts(pkt, inSt->time_base, outSt->time_base);
    pkt->stream_index = outSt->index;
    if (av_interleaved_write_frame(ofmt, pkt) < 0) {
      errorText = "mux write failed";
      break;
    }
    av_packet_unref(pkt);
    if (writeVideo) {
      vhas = readNext(&vc);
    } else {
      ahas = readNext(&ac);
    }
  }
  av_write_trailer(ofmt);
  avio_closep(&ofmt->pb);
  avformat_free_context(ofmt);
  avformat_close_input(&vfmt);
  if (afmt) {
    avformat_close_input(&afmt);
  }
  if (!errorText.empty()) {
    std::remove(out.c_str());
    return false;
  }
  return true;
}

IMediaComposer* createMediaComposer() {
  return new (std::nothrow) MediaComposerFF();
}

}  // namespace avox
