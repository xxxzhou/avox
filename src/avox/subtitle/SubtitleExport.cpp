#include "avox/AvoxSubtitle.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#ifdef AVOX_ENABLE_FFMPEG
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/mathematics.h>
}
#endif

#include "../module/LogHelper.hpp"

namespace avox {
namespace {

#ifdef AVOX_ENABLE_FFMPEG
bool isTextSubtitleId(AVCodecID id) {
  switch (id) {
    case AV_CODEC_ID_SUBRIP:
    case AV_CODEC_ID_ASS:
    case AV_CODEC_ID_SSA:
    case AV_CODEC_ID_MOV_TEXT:
    case AV_CODEC_ID_WEBVTT:
    case AV_CODEC_ID_TEXT:
    case AV_CODEC_ID_SUBVIEWER:
    case AV_CODEC_ID_SUBVIEWER1:
    case AV_CODEC_ID_SAMI:
    case AV_CODEC_ID_REALTEXT:
    case AV_CODEC_ID_STL:
    case AV_CODEC_ID_TTML:
    case AV_CODEC_ID_MICRODVD:
    case AV_CODEC_ID_MPL2:
    case AV_CODEC_ID_VPLAYER:
    case AV_CODEC_ID_PJS:
    case AV_CODEC_ID_JACOSUB:
    case AV_CODEC_ID_HDMV_TEXT_SUBTITLE:
      return true;
    default:
      return false;
  }
}
#endif  // AVOX_ENABLE_FFMPEG

std::string formatSrtTime(int64_t ms) {
  if (ms < 0) {
    ms = 0;
  }
  const int64_t hours = ms / 3600000;
  const int64_t minutes = (ms / 60000) % 60;
  const int64_t seconds = (ms / 1000) % 60;
  const int64_t millis = ms % 1000;
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%02lld:%02lld:%02lld,%03lld",
                static_cast<long long>(hours),
                static_cast<long long>(minutes),
                static_cast<long long>(seconds),
                static_cast<long long>(millis));
  return buf;
}

std::string trimLine(const std::string& value) {
  size_t begin = 0;
  size_t end = value.size();
  while (begin < end && (value[begin] == ' ' || value[begin] == '\t')) {
    ++begin;
  }
  while (end > begin && (value[end - 1] == ' ' || value[end - 1] == '\t')) {
    --end;
  }
  return value.substr(begin, end - begin);
}

// SRT/文本轨: 统一换行并去掉首尾空行, 不动行内的合法空格。
std::string normalizePlainText(const std::string& raw) {
  std::string text;
  text.reserve(raw.size());
  for (size_t i = 0; i < raw.size(); ++i) {
    if (raw[i] == '\r') {
      if (i + 1 < raw.size() && raw[i + 1] == '\n') {
        ++i;
      }
      text += '\n';
    } else {
      text += raw[i];
    }
  }
  if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
      static_cast<unsigned char>(text[1]) == 0xBB &&
      static_cast<unsigned char>(text[2]) == 0xBF) {
    text.erase(0, 3);
  }
  std::vector<std::string> lines;
  size_t start = 0;
  while (start <= text.size()) {
    const size_t pos = text.find('\n', start);
    const size_t end = pos == std::string::npos ? text.size() : pos;
    lines.push_back(text.substr(start, end - start));
    if (pos == std::string::npos) {
      break;
    }
    start = pos + 1;
  }
  while (!lines.empty() && trimLine(lines.front()).empty()) {
    lines.erase(lines.begin());
  }
  while (!lines.empty() && trimLine(lines.back()).empty()) {
    lines.pop_back();
  }
  std::string result;
  for (size_t i = 0; i < lines.size(); ++i) {
    if (i > 0) {
      result += '\n';
    }
    result += lines[i];
  }
  return result;
}

#ifdef AVOX_ENABLE_FFMPEG
// ASS/SSA: 去 {\\...} 覆盖标签, 还原 \N/\n/\h; 只保留可读文本。
std::string cleanAssText(const char* raw) {
  if (!raw) {
    return {};
  }
  std::string out;
  bool inOverride = false;
  for (size_t i = 0; raw[i] != '\0'; ++i) {
    const char c = raw[i];
    if (!inOverride && c == '{') {
      inOverride = true;
      continue;
    }
    if (inOverride) {
      if (c == '}') {
        inOverride = false;
      }
      continue;
    }
    if (c == '\\' && raw[i + 1] != '\0') {
      const char next = raw[i + 1];
      if (next == 'N' || next == 'n') {
        out += '\n';
        ++i;
        continue;
      }
      if (next == 'h') {
        out += ' ';
        ++i;
        continue;
      }
    }
    out += c;
  }
  return normalizePlainText(out);
}

// FFmpeg 字幕解码器的 ASS 包装是 8 段(ReadOrder/Layer/Style/Name/Margin/
// Effect), 完整 Dialogue 行是 9 段; 覆盖标签里的逗号不算分隔符。
std::string cleanAssDialog(const char* raw) {
  if (!raw) {
    return {};
  }
  const char* text = raw;
  int fields = 8;
  if (std::strncmp(text, "Dialogue:", 9) == 0) {
    text += 9;
    fields = 9;
  }
  // 只有形如数字开头的字段行才剥离, 避免误伤正文里的逗号;
  // 覆盖标签 \pos(320,320) 里的逗号不算字段分隔符
  if (std::isdigit(static_cast<unsigned char>(text[0]))) {
    const char* scan = text;
    int commas = 0;
    bool inOverride = false;
    while (*scan != '\0' && commas < fields) {
      if (*scan == '{') {
        inOverride = true;
      } else if (*scan == '}') {
        inOverride = false;
      } else if (*scan == ',' && !inOverride) {
        ++commas;
      }
      ++scan;
    }
    if (commas == fields) {
      text = scan;
    }
  }
  return cleanAssText(text);
}

std::string collectText(const AVSubtitle& sub, bool& bitmap) {
  std::vector<std::string> parts;
  for (uint32_t i = 0; i < sub.num_rects; ++i) {
    const AVSubtitleRect* rect = sub.rects[i];
    if (!rect) {
      continue;
    }
    std::string part;
    if (rect->type == SUBTITLE_ASS) {
      part = cleanAssDialog(rect->ass ? rect->ass : rect->text);
    } else if (rect->type == SUBTITLE_TEXT) {
      part = normalizePlainText(rect->text ? rect->text : "");
    } else {
      bitmap = true;
      continue;
    }
    if (!part.empty()) {
      parts.push_back(std::move(part));
    }
  }
  std::string joined;
  for (size_t i = 0; i < parts.size(); ++i) {
    if (i > 0) {
      joined += '\n';
    }
    joined += parts[i];
  }
  return joined;
}

int64_t packetTimeMs(const AVPacket* packet, const AVStream* stream) {
  if (!packet || packet->pts == AV_NOPTS_VALUE) {
    return AV_NOPTS_VALUE;
  }
  return av_rescale_q(packet->pts, stream->time_base, AVRational{1, 1000});
}

int64_t packetDurationMs(const AVPacket* packet, const AVStream* stream) {
  if (!packet || packet->duration <= 0) {
    return 0;
  }
  return av_rescale_q(packet->duration, stream->time_base, AVRational{1, 1000});
}
#endif  // AVOX_ENABLE_FFMPEG

class SrtWriterImpl : public ISrtWriter {
 public:
  bool open(const char* path) override {
    if (!path || path[0] == '\0') {
      return false;
    }
    path_ = path;
    items_.clear();
    return true;
  }

  bool add(int64_t startMs, int64_t endMs, const char* text) override {
    if (path_.empty() || !text) {
      return false;
    }
    std::string value = normalizePlainText(text);
    if (value.empty()) {
      return false;
    }
    Item item;
    item.startMs = std::max<int64_t>(0, startMs);
    item.endMs = endMs;
    if (item.endMs <= item.startMs) {
      item.endMs = item.startMs + 1;
    }
    item.text = std::move(value);
    items_.push_back(std::move(item));
    return true;
  }

  bool close() override {
    if (path_.empty() || items_.empty()) {
      path_.clear();
      items_.clear();
      return false;
    }
    std::stable_sort(items_.begin(), items_.end(),
                     [](const Item& a, const Item& b) {
                       return a.startMs < b.startMs;
                     });
    std::string content;
    content.reserve(items_.size() * 96);
    for (size_t i = 0; i < items_.size(); ++i) {
      content += std::to_string(i + 1);
      content += '\n';
      content += formatSrtTime(items_[i].startMs);
      content += " --> ";
      content += formatSrtTime(items_[i].endMs);
      content += '\n';
      content += items_[i].text;
      content += "\n\n";
    }
    const std::string path = path_;
    path_.clear();
    items_.clear();
    std::ofstream file(std::filesystem::u8path(path),
                       std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
      return false;
    }
    file.write(content.data(), static_cast<std::streamsize>(content.size()));
    return file.good();
  }

 private:
  struct Item {
    int64_t startMs = 0;
    int64_t endMs = 0;
    std::string text;
  };
  std::string path_;
  std::vector<Item> items_;
};

}  // namespace

ISrtWriter* createSrtWriter() { return new SrtWriterImpl(); }

#ifdef AVOX_ENABLE_FFMPEG
bool isTextSubtitleCodec(const char* codecName) {
  if (!codecName || codecName[0] == '\0') {
    return false;
  }
  const AVCodec* codec = avcodec_find_decoder_by_name(codecName);
  return codec != nullptr && isTextSubtitleId(codec->id);
}

int32_t exportSubtitleTrack(const char* url, int32_t streamIndex,
                            const char* outPath) {
  if (!url || url[0] == '\0' || !outPath || outPath[0] == '\0' ||
      streamIndex < 0) {
    return -1;
  }

  AVFormatContext* format = nullptr;
  if (avformat_open_input(&format, url, nullptr, nullptr) < 0 || !format) {
    LOGFLF(LogLevel::warn, "subtitle export: open failed: ", url);
    return -1;
  }
  if (avformat_find_stream_info(format, nullptr) < 0) {
    LOGFLF(LogLevel::warn, "subtitle export: stream info failed: ", url);
    avformat_close_input(&format);
    return -1;
  }

  AVStream* stream = nullptr;
  for (unsigned i = 0; i < format->nb_streams; ++i) {
    if (static_cast<int32_t>(i) == streamIndex && format->streams[i] &&
        format->streams[i]->codecpar &&
        format->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE) {
      stream = format->streams[i];
      break;
    }
  }
  if (!stream) {
    avformat_close_input(&format);
    return -2;
  }
  if (!isTextSubtitleId(stream->codecpar->codec_id)) {
    LOGFLF(LogLevel::info, "subtitle export: bitmap track unsupported: ",
           avcodec_get_name(stream->codecpar->codec_id));
    avformat_close_input(&format);
    return -3;
  }

  const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
  if (!codec) {
    avformat_close_input(&format);
    return -4;
  }
  AVCodecContext* ctx = avcodec_alloc_context3(codec);
  if (!ctx) {
    avformat_close_input(&format);
    return -4;
  }
  ctx->pkt_timebase = stream->time_base;
  if (avcodec_parameters_to_context(ctx, stream->codecpar) < 0 ||
      avcodec_open2(ctx, codec, nullptr) < 0) {
    avcodec_free_context(&ctx);
    avformat_close_input(&format);
    return -4;
  }

  // 字幕包很小, 先收齐以便用下一包的 pts 补出缺失的结束时间。
  std::vector<AVPacket*> packets;
  AVPacket* packet = av_packet_alloc();
  bool readFailed = false;
  if (packet) {
    while (av_read_frame(format, packet) >= 0) {
      if (packet->stream_index == streamIndex) {
        AVPacket* copy = av_packet_clone(packet);
        if (copy) {
          packets.push_back(copy);
        }
      }
      av_packet_unref(packet);
    }
    av_packet_free(&packet);
  } else {
    readFailed = true;
  }

  struct Item {
    int64_t startMs = 0;
    int64_t endMs = 0;
    std::string text;
  };
  std::vector<Item> items;
  bool anyBitmap = false;
  bool anyDecoded = false;

  for (size_t i = 0; i < packets.size(); ++i) {
    AVSubtitle sub;
    std::memset(&sub, 0, sizeof(sub));
    int got = 0;
    const int ret = avcodec_decode_subtitle2(ctx, &sub, &got, packets[i]);
    if (ret < 0) {
      avsubtitle_free(&sub);
      continue;
    }
    if (got) {
      anyDecoded = true;
      int64_t baseMs = packetTimeMs(packets[i], stream);
      if (baseMs == AV_NOPTS_VALUE) {
        baseMs = sub.pts != AV_NOPTS_VALUE ? sub.pts / 1000 : 0;
      }
      int64_t startMs = baseMs + static_cast<int64_t>(sub.start_display_time);
      int64_t endMs = startMs + 1000;
      if (sub.end_display_time > sub.start_display_time) {
        endMs = baseMs + static_cast<int64_t>(sub.end_display_time);
      } else {
        const int64_t duration = packetDurationMs(packets[i], stream);
        if (duration > 0) {
          endMs = startMs + duration;
        } else if (i + 1 < packets.size()) {
          const int64_t nextMs = packetTimeMs(packets[i + 1], stream);
          if (nextMs != AV_NOPTS_VALUE && nextMs > startMs) {
            endMs = nextMs;
          }
        }
      }
      std::string text = collectText(sub, anyBitmap);
      if (!text.empty()) {
        Item item;
        item.startMs = std::max<int64_t>(0, startMs);
        item.endMs = std::max(item.startMs + 1, endMs);
        item.text = std::move(text);
        items.push_back(std::move(item));
      }
    }
    avsubtitle_free(&sub);
  }

  for (AVPacket* item : packets) {
    av_packet_free(&item);
  }
  avcodec_free_context(&ctx);
  avformat_close_input(&format);

  if (items.empty()) {
    if (anyBitmap && !anyDecoded) {
      return -3;
    }
    return readFailed ? -4 : -6;
  }

  ISrtWriter* writer = createSrtWriter();
  if (!writer || !writer->open(outPath)) {
    delete writer;
    return -5;
  }
  int32_t cues = 0;
  for (const Item& item : items) {
    if (writer->add(item.startMs, item.endMs, item.text.c_str())) {
      ++cues;
    }
  }
  const bool written = cues > 0 && writer->close();
  delete writer;
  if (!written) {
    return cues > 0 ? -5 : -6;
  }
  LOGFLF(LogLevel::info, "subtitle export: stream=", streamIndex,
         " cues=", static_cast<int64_t>(cues), " out=", outPath);
  return 0;
}
#else
bool isTextSubtitleCodec(const char*) { return false; }

int32_t exportSubtitleTrack(const char*, int32_t, const char*) { return -1; }
#endif  // AVOX_ENABLE_FFMPEG

}  // namespace avox
