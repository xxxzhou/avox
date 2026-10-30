#include "MediaReaderFF.hpp"

#include <chrono>
#include <cstring>
#include <new>

#include "FFHelper.hpp"
#include "avox/module/LogHelper.hpp"
#include "avox/source/PacketBuf.hpp"
#include "avox/video/MediaThumbRenderer.hpp"
#include "avox/video/MetaExtractor.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/dict.h>
#include <libavutil/dovi_meta.h>
#include <libavutil/error.h>
#include <libavutil/pixdesc.h>
}

#include "MediaReaderUtil.hpp"

namespace avox {

namespace {

// 与 ak_probe 同序: avg_frame_rate → r_frame_rate(ffFps 是
// codecpar->framerate → avg, probe 口径不同, 不用)
double streamFps(const AVStream* st) {
  if (st->avg_frame_rate.den > 0) {
    return av_q2d(st->avg_frame_rate);
  }
  if (st->r_frame_rate.den > 0) {
    return av_q2d(st->r_frame_rate);
  }
  return 0.0;
}

// pts(时间基 tb) - origin → 毫秒; pts=NOPTS 或 tb 非法返回 -1
int64_t ptsToMs(int64_t pts, const AVRational& tb, int64_t origin) {
  if (pts == AV_NOPTS_VALUE || tb.num <= 0 || tb.den <= 0) {
    return -1;
  }
  return av_rescale_q(pts - origin, tb, AVRational{1, 1000});
}

std::string ffmpegErrMsg(int err) {
  char buf[AV_ERROR_MAX_STRING_SIZE] = {};
  av_strerror(err, buf, sizeof(buf));
  return buf;
}

// 容器 DOVI conf(dvcc/dvcC) → dvProfile(取法同 IOParseFF)
int32_t streamDvProfile(const AVStream* st) {
  if (!st || !st->codecpar) {
    return 0;
  }
  const AVCodecParameters* par = st->codecpar;
  for (int32_t k = 0; k < par->nb_coded_side_data; ++k) {
    const AVPacketSideData& sd = par->coded_side_data[k];
    if (sd.type == AV_PKT_DATA_DOVI_CONF &&
        sd.size >= (int)sizeof(AVDOVIDecoderConfigurationRecord)) {
      return ((const AVDOVIDecoderConfigurationRecord*)sd.data)->dv_profile;
    }
  }
  return 0;
}

// display matrix → 顺时针显示角(0/90/180/270), 与 fillSourceInfo 同口径
int32_t streamRotation(const AVStream* st) {
  if (!st || !st->codecpar) {
    return 0;
  }
  const AVCodecParameters* par = st->codecpar;
  for (int32_t k = 0; k < par->nb_coded_side_data; ++k) {
    const AVPacketSideData& sd = par->coded_side_data[k];
    if (sd.type == AV_PKT_DATA_DISPLAYMATRIX &&
        sd.size >= 9 * (int)sizeof(int32_t)) {
      return displayMatrixRotation((const int32_t*)sd.data);
    }
  }
  return 0;
}

// 帧级色彩信息优先(流级可能漏标/逐帧变化); 未标记量程按 limited, 与抓帧旧口径一致
ColorSpaceDesc frameColorSpace(const AVFrame* frame) {
  ColorSpaceDesc cs = {};
  switch (frame->colorspace) {
    case AVCOL_SPC_BT709:
      cs.standard = YuvStandard::bt709;
      break;
    case AVCOL_SPC_BT2020_NCL:
    case AVCOL_SPC_BT2020_CL:
      cs.standard = YuvStandard::bt2020;
      break;
    case AVCOL_SPC_SMPTE170M:
    case AVCOL_SPC_BT470BG:
    case AVCOL_SPC_FCC:
    case AVCOL_SPC_SMPTE240M:
    case AVCOL_SPC_CHROMA_DERIVED_NCL:
    case AVCOL_SPC_CHROMA_DERIVED_CL:
      cs.standard = YuvStandard::bt601;
      break;
    default:
      cs.standard =
          frame->height >= 720 ? YuvStandard::bt709 : YuvStandard::bt601;
      break;
  }
  cs.range = frame->color_range == AVCOL_RANGE_JPEG ? YuvRange::full
                                                    : YuvRange::limited;
  switch (frame->color_trc) {
    case AVCOL_TRC_SMPTE2084:
      cs.transfer = YuvTransfer::pq;
      break;
    case AVCOL_TRC_ARIB_STD_B67:
      cs.transfer = YuvTransfer::hlg;
      break;
    case AVCOL_TRC_LINEAR:
      cs.transfer = YuvTransfer::linear;
      break;
    default:
      cs.transfer = YuvTransfer::gamma;
      break;
  }
  return cs;
}

}  // namespace

MediaReaderFF::MediaReaderFF() {}

MediaReaderFF::~MediaReaderFF() { closeInput(); }

void MediaReaderFF::setObserver(IMediaReaderOb* ob) { observer = ob; }

int MediaReaderFF::interruptCb(void* opaque) {
  return opaque && static_cast<MediaReaderFF*>(opaque)->interruptIo() ? 1 : 0;
}

void MediaReaderFF::notifyState(MediaReaderState from, MediaReaderState to) {
  state = to;
  if (observer) {
    observer->onStateChange(from, to);
  }
}

void MediaReaderFF::reportError(MediaOp op, MediaError code,
                                const std::string& message) {
  errorCode = code;
  errorMessage = message;
  if (observer) {
    observer->onError(op, code, errorMessage.c_str());
  }
}

bool MediaReaderFF::reject(MediaError code, const std::string& message) {
  errorCode = code;
  errorMessage = message;
  return false;
}

bool MediaReaderFF::open(const char* uri) {
  if (!uri || !*uri) {
    return reject(MediaError::invalidArgument, "uri is empty");
  }
  if (state != MediaReaderState::idle) {
    return reject(MediaError::invalidState, "open requires idle state");
  }
  if (isNetworkUri(uri)) {
    return reject(MediaError::unsupported, "v1 supports local files only");
  }
  errorCode = MediaError::none;
  errorMessage.clear();
  bCancel = false;
  notifyState(MediaReaderState::idle, MediaReaderState::opening);
  const bool ok = openInput(uri);
  if (!ok) {
    // 失败收尾: onError 先于状态回退(契约 §4.2)
    reportError(MediaOp::open, errorCode, errorMessage);
    notifyState(MediaReaderState::opening, MediaReaderState::idle);
    closeInput();
  } else {
    notifyState(MediaReaderState::opening, MediaReaderState::ready);
    if (observer) {
      observer->onReady();
    }
  }
  return true;  // 请求已接受, 结果只经回调
}

bool MediaReaderFF::openInput(const char* uri) {
  // 先建上下文装 interrupt callback: cancel 能打断 open/find_stream_info 阻塞
  fmtCtx = avformat_alloc_context();
  if (!fmtCtx) {
    errorCode = MediaError::inputOpenFailed;
    errorMessage = "avformat_alloc_context failed";
    return false;
  }
  fmtCtx->interrupt_callback.callback = &MediaReaderFF::interruptCb;
  fmtCtx->interrupt_callback.opaque = this;
  int ret = avformat_open_input(&fmtCtx, uri, nullptr, nullptr);
  if (ret < 0) {
    fmtCtx = nullptr;  // 失败时 ffmpeg 已释放并置空
    if (bCancel) {
      errorCode = MediaError::cancelled;
      errorMessage = "cancelled";
    } else {
      errorCode = MediaError::inputOpenFailed;
      errorMessage = ffmpegErrMsg(ret);
    }
    return false;
  }
  ret = avformat_find_stream_info(fmtCtx, nullptr);
  if (ret < 0 || bCancel) {
    if (bCancel) {
      errorCode = MediaError::cancelled;
      errorMessage = "cancelled";
    } else {
      errorCode = MediaError::findStreamInfoFailed;
      errorMessage = ffmpegErrMsg(ret);
    }
    return false;
  }
  fillSourceInfo();
  LOGFLF(LogLevel::info, "[media-reader] open ok:", uri, " video:",
         sourceInfo->videoSize(), " audio:", sourceInfo->audioSize(),
         " subtitle:", sourceInfo->subtitleSize(),
         " duration:", sourceInfo->durationMs(), " seekable:",
         sourceInfo->canSeek());
  return true;
}

void MediaReaderFF::fillSourceInfo() {
  sourceInfo = std::make_unique<MediaSourceInfo>();
  AVFormatContext* ctx = fmtCtx;
  const bool seekable =
      ctx->pb && (ctx->pb->seekable & AVIO_SEEKABLE_NORMAL) != 0;
  sourceInfo->setContainerInfo(
      ctx->duration != AV_NOPTS_VALUE ? ctx->duration / (AV_TIME_BASE / 1000)
                                      : 0,
      ctx->bit_rate,
      (ctx->iformat && ctx->iformat->name) ? ctx->iformat->name : "",
      seekable);
  for (unsigned i = 0; i < ctx->nb_streams; ++i) {
    AVStream* st = ctx->streams[i];
    if (!st || !st->codecpar) {
      continue;
    }
    // 封面图等附加静态图流不是可播放视频轨, 与 IOParseFF/ak_frames 口径一致
    if (st->disposition & AV_DISPOSITION_ATTACHED_PIC) {
      continue;
    }
    const AVCodecParameters* par = st->codecpar;
    const char* rawName = avcodec_get_name(par->codec_id);
    const std::string codecName = rawName ? rawName : "";
    if (par->codec_type == AVMEDIA_TYPE_VIDEO) {
      VTrackDesc vdesc = {};
      vdesc.trackId = st->index;
      vdesc.codecId = ffVCodec(par->codec_id);
      vdesc.desc.width = par->width;
      vdesc.desc.height = par->height;
      vdesc.desc.fps = streamFps(st);
      vdesc.desc.type = ffYuvType((AVPixelFormat)par->format);
      vdesc.desc.colorSpace = ffColorSpace(st->codecpar);
      vdesc.desc.codecTag = (int32_t)par->codec_tag;
      // 容器 DOVI conf(dvcc/dvcC) → dvProfile(取法同 IOParseFF)
      for (int32_t k = 0; k < par->nb_coded_side_data; ++k) {
        const AVPacketSideData& sd = par->coded_side_data[k];
        if (sd.type == AV_PKT_DATA_DOVI_CONF &&
            sd.size >= (int)sizeof(AVDOVIDecoderConfigurationRecord)) {
          vdesc.dvProfile =
              ((const AVDOVIDecoderConfigurationRecord*)sd.data)->dv_profile;
          break;
        }
      }
      // display matrix → 顺时针显示角(0/90/180/270), 算法同 IOParseFF
      for (int32_t k = 0; k < par->nb_coded_side_data; ++k) {
        const AVPacketSideData& sd = par->coded_side_data[k];
        if (sd.type == AV_PKT_DATA_DISPLAYMATRIX &&
            sd.size >= 9 * (int)sizeof(int32_t)) {
          vdesc.rotation = displayMatrixRotation((const int32_t*)sd.data);
          break;
        }
      }
      sourceInfo->addVideoDesc(vdesc);
      sourceInfo->addVideoCodecName(codecName);
    } else if (par->codec_type == AVMEDIA_TYPE_AUDIO) {
      ATrackDesc adesc = {};
      adesc.trackId = st->index;
      // probe 列全口径: codecId 映射不出的(none)也照列, 原始名给上层显示
      adesc.codecId = ffACodec(par->codec_id);
      adesc.desc.sampleRate = par->sample_rate;
      adesc.desc.format = ffAudioFromat(par->format);
      adesc.desc.channels = (uint16_t)par->ch_layout.nb_channels;
      adesc.desc.blockAlign = par->block_align;
      sourceInfo->addAudioDesc(adesc);
      sourceInfo->addAudioCodecName(codecName);
    } else if (par->codec_type == AVMEDIA_TYPE_SUBTITLE) {
      // probe 列全口径: 不按 ffSCodec 过滤, 文本/图形由原始名判定
      std::string lang;
      std::string title;
      if (auto* e = av_dict_get(st->metadata, "language", nullptr, 0)) {
        lang = e->value ? e->value : "";
      }
      if (auto* e = av_dict_get(st->metadata, "title", nullptr, 0)) {
        title = e->value ? e->value : "";
      }
      sourceInfo->addSubtitleDesc(
          st->index, ffSCodec(par->codec_id), lang, title,
          (st->disposition & AV_DISPOSITION_FORCED) != 0, codecName,
          (st->disposition & AV_DISPOSITION_DEFAULT) != 0);
    }
  }
}

bool MediaReaderFF::buildIndex(const MediaIndexOptions& options) {
  if (state != MediaReaderState::ready) {
    return reject(MediaError::invalidState, "buildIndex requires ready state");
  }
  errorCode = MediaError::none;
  errorMessage.clear();
  bCancel = false;
  notifyState(MediaReaderState::ready, MediaReaderState::indexing);
  const bool ok = buildIndexImpl(options);
  if (!ok) {
    // 失败收尾: onError 先于状态回退(契约 §4.2)
    reportError(MediaOp::index, errorCode, errorMessage);
  }
  notifyState(MediaReaderState::indexing, MediaReaderState::ready);
  return true;  // 请求已接受, 结果只经回调
}

bool MediaReaderFF::buildIndexImpl(const MediaIndexOptions& options) {
  int32_t buckets = options.buckets;
  if (buckets < 64) {
    buckets = 64;
  } else if (buckets > 4000) {
    buckets = 4000;
  }
  // 选轨: 显式 stream index 或 best stream(与 ak_index 同口径, keyframes
  // 来源轨保持不变); "第一条非封面"只用于 grabFrame
  int videoIndex = options.videoTrack;
  if (videoIndex < 0) {
    videoIndex =
        av_find_best_stream(fmtCtx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
  } else if (videoIndex >= (int)fmtCtx->nb_streams ||
             !fmtCtx->streams[videoIndex] ||
             fmtCtx->streams[videoIndex]->codecpar->codec_type !=
                 AVMEDIA_TYPE_VIDEO) {
    errorCode = MediaError::invalidArgument;
    errorMessage = "videoTrack is not a video stream";
    return false;
  }
  int audioIndex = options.audioTrack;
  if (audioIndex < 0) {
    audioIndex =
        av_find_best_stream(fmtCtx, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
  } else if (audioIndex >= (int)fmtCtx->nb_streams ||
             !fmtCtx->streams[audioIndex] ||
             fmtCtx->streams[audioIndex]->codecpar->codec_type !=
                 AVMEDIA_TYPE_AUDIO) {
    errorCode = MediaError::invalidArgument;
    errorMessage = "audioTrack is not an audio stream";
    return false;
  }
  AVStream* video = videoIndex >= 0 ? fmtCtx->streams[videoIndex] : nullptr;
  AVStream* audio = audioIndex >= 0 ? fmtCtx->streams[audioIndex] : nullptr;
  const int64_t videoOrigin =
      video && video->start_time != AV_NOPTS_VALUE ? video->start_time : 0;
  const int64_t audioOrigin =
      audio && audio->start_time != AV_NOPTS_VALUE ? audio->start_time : 0;
  // 有效时长回退前两步: 容器 → stream 最大(第三步音频末帧在循环后)
  int64_t durationMs = 0;
  if (fmtCtx->duration != AV_NOPTS_VALUE) {
    durationMs = fmtCtx->duration / (AV_TIME_BASE / 1000);
  }
  if (durationMs <= 0) {
    for (unsigned i = 0; i < fmtCtx->nb_streams; ++i) {
      const AVStream* st = fmtCtx->streams[i];
      if (st && st->duration != AV_NOPTS_VALUE) {
        const int64_t ms = av_rescale_q(st->duration, st->time_base,
                                        AVRational{1, 1000});
        if (ms > durationMs) {
          durationMs = ms;
        }
      }
    }
  }
  // 音频解码器(打不开只影响 waveAvailable, 不算 index 失败)
  const AVCodec* decoder = nullptr;
  AVCodecContext* audioCtx = nullptr;
  if (audio) {
    decoder = avcodec_find_decoder(audio->codecpar->codec_id);
    if (decoder) {
      audioCtx = avcodec_alloc_context3(decoder);
    }
    if (audioCtx) {
      audioCtx->pkt_timebase = audio->time_base;
      if (avcodec_parameters_to_context(audioCtx, audio->codecpar) < 0 ||
          avcodec_open2(audioCtx, decoder, nullptr) < 0) {
        avcodec_free_context(&audioCtx);
      }
    }
  }
  const bool waveAvailable = audioCtx != nullptr;
  AVPacket* packet = av_packet_alloc();
  AVFrame* frame = av_frame_alloc();
  if (!packet || !frame) {
    if (packet) av_packet_free(&packet);
    if (frame) av_frame_free(&frame);
    avcodec_free_context(&audioCtx);
    errorCode = MediaError::other;
    errorMessage = "alloc packet/frame failed";
    return false;
  }
  std::vector<std::pair<int64_t, double>> audioPeaks;
  indexKeyframes.clear();
  int64_t lastMs = 0;
  int64_t audioFallbackMs = 0;
  const int32_t sampleRate = audio ? audio->codecpar->sample_rate : 0;
  // 逐帧收 (ms, 线性峰值), duration 确定后再映射到桶(修 ak_index
  // "时长靠音频末帧推出时 add_peak 不写桶→peaks 全 0" 的隐藏 bug)
  auto drainAudio = [&]() {
    while (audioCtx && avcodec_receive_frame(audioCtx, frame) >= 0) {
      int64_t ms = -1;
      if (frame->best_effort_timestamp != AV_NOPTS_VALUE) {
        ms = ptsToMs(frame->best_effort_timestamp, audio->time_base,
                     audioOrigin);
      }
      if (ms < 0) {
        ms = audioFallbackMs;
      }
      audioPeaks.emplace_back(
          ms, mediaFramePeak(frame->extended_data,
                             frame->ch_layout.nb_channels, frame->nb_samples,
                             ffAudioFromat(frame->format)));
      if (ms > lastMs) {
        lastMs = ms;
      }
      if (sampleRate > 0) {
        audioFallbackMs = ms + (int64_t)frame->nb_samples * 1000 / sampleRate;
      }
      av_frame_unref(frame);
    }
  };
  // 进度: 有文件大小才有, 1% 或 50ms 限频(回调在调用线程, 同步实现)
  int32_t lastPercent = -1;
  auto lastTick = std::chrono::steady_clock::now();
  auto reportProgress = [&]() {
    if (!observer || !fmtCtx->pb) {
      return;
    }
    const int64_t size = avio_size(fmtCtx->pb);
    if (size <= 0) {
      return;
    }
    const int64_t pos = avio_tell(fmtCtx->pb);
    int32_t percent = pos > 0 ? (int32_t)(pos * 100 / size) : 0;
    if (percent > 99) {
      percent = 99;
    }
    const auto now = std::chrono::steady_clock::now();
    if (percent == lastPercent ||
        std::chrono::duration_cast<std::chrono::milliseconds>(now - lastTick)
                .count() < 50) {
      return;
    }
    lastPercent = percent;
    lastTick = now;
    observer->onIndexProgress(percent);
  };
  while (av_read_frame(fmtCtx, packet) >= 0) {
    if (bCancel) {
      av_packet_unref(packet);
      errorCode = MediaError::cancelled;
      errorMessage = "cancelled";
      av_packet_free(&packet);
      av_frame_free(&frame);
      avcodec_free_context(&audioCtx);
      indexKeyframes.clear();
      return false;
    }
    if (video && packet->stream_index == videoIndex &&
        (packet->flags & AV_PKT_FLAG_KEY) != 0) {
      // pts 缺失回退 dts(与 ak_index 一致)
      const int64_t ms = ptsToMs(
          packet->pts != AV_NOPTS_VALUE ? packet->pts : packet->dts,
          video->time_base, videoOrigin);
      if (ms >= 0 && (indexKeyframes.empty() || ms > indexKeyframes.back())) {
        indexKeyframes.push_back(ms);
      }
    } else if (audioCtx && audio && packet->stream_index == audioIndex) {
      if (avcodec_send_packet(audioCtx, packet) >= 0) {
        drainAudio();
      }
    }
    reportProgress();
    av_packet_unref(packet);
  }
  // EOF: 排空解码器尾帧
  if (audioCtx) {
    avcodec_send_packet(audioCtx, nullptr);
    drainAudio();
  }
  av_packet_free(&packet);
  av_frame_free(&frame);
  avcodec_free_context(&audioCtx);
  if (bCancel) {
    errorCode = MediaError::cancelled;
    errorMessage = "cancelled";
    indexKeyframes.clear();
    return false;
  }
  // 时长回退第三步: 音频末帧推算
  if (durationMs <= 0) {
    durationMs = lastMs;
  }
  int64_t bucketMs = 0;
  if (durationMs > 0) {
    bucketMs = durationMs / buckets;
    if (bucketMs < 1) {
      bucketMs = 1;
    }
  }
  if (waveAvailable) {
    mapPeaksToBuckets(audioPeaks, buckets, durationMs, indexPeaks);
  } else {
    indexPeaks.clear();  // 与 ak_index 一致: 解码器打不开 → peaks 空数组
  }
  MediaIndexResult result;
  result.durationMs = durationMs;
  result.bucketMs = bucketMs;
  result.hasAudio = audio != nullptr;
  result.waveAvailable = waveAvailable;
  result.peaks = indexPeaks.empty() ? nullptr : indexPeaks.data();
  result.peakCount = (int32_t)indexPeaks.size();
  result.keyframesMs =
      indexKeyframes.empty() ? nullptr : indexKeyframes.data();
  result.keyframeCount = (int32_t)indexKeyframes.size();
  if (observer) {
    observer->onIndexDone(result);
  }
  return true;
}

bool MediaReaderFF::grabFrame(const MediaFrameRequest& request) {
  if (state != MediaReaderState::ready) {
    return reject(MediaError::invalidState, "grabFrame requires ready state");
  }
  errorCode = MediaError::none;
  errorMessage.clear();
  bCancel = false;
  notifyState(MediaReaderState::ready, MediaReaderState::grabbing);
  const bool ok = grabFrameImpl(request);
  if (!ok) {
    // 失败收尾: onError 先于状态回退(契约 §4.2)
    reportError(MediaOp::frame, errorCode, errorMessage);
  }
  notifyState(MediaReaderState::grabbing, MediaReaderState::ready);
  return true;  // 请求已接受, 结果只经回调
}

bool MediaReaderFF::grabFrameImpl(const MediaFrameRequest& request) {
  if (!MediaThumbRenderer::available()) {
    errorCode = MediaError::renderUnavailable;
    errorMessage = "vulkan is not available in this build";
    return false;
  }
  // 选轨: 显式 stream index 或第一条非封面且有宽高的视频轨
  int videoIndex = request.videoTrack;
  if (videoIndex >= 0) {
    const AVStream* st =
        videoIndex < (int)fmtCtx->nb_streams ? fmtCtx->streams[videoIndex]
                                             : nullptr;
    if (!st || !st->codecpar ||
        st->codecpar->codec_type != AVMEDIA_TYPE_VIDEO ||
        (st->disposition & AV_DISPOSITION_ATTACHED_PIC)) {
      errorCode = MediaError::invalidArgument;
      errorMessage = "videoTrack is not a playable video stream";
      return false;
    }
  } else {
    for (unsigned i = 0; i < fmtCtx->nb_streams; ++i) {
      const AVStream* st = fmtCtx->streams[i];
      if (!st || !st->codecpar ||
          st->codecpar->codec_type != AVMEDIA_TYPE_VIDEO ||
          (st->disposition & AV_DISPOSITION_ATTACHED_PIC)) {
        continue;
      }
      if (st->codecpar->width <= 0 || st->codecpar->height <= 0) {
        continue;
      }
      videoIndex = (int)i;
      break;
    }
  }
  if (videoIndex < 0) {
    errorCode = MediaError::noVideo;
    errorMessage = "no playable video track";
    return false;
  }
  AVStream* stream = fmtCtx->streams[videoIndex];
  const AVCodec* decoder = avcodec_find_decoder(stream->codecpar->codec_id);
  if (!decoder) {
    errorCode = MediaError::decoderMissing;
    errorMessage = "no decoder for this video stream";
    return false;
  }
  AVCodecContext* codec = avcodec_alloc_context3(decoder);
  if (!codec) {
    errorCode = MediaError::decoderMissing;
    errorMessage = "avcodec_alloc_context3 failed";
    return false;
  }
  if (avcodec_parameters_to_context(codec, stream->codecpar) < 0 ||
      avcodec_open2(codec, decoder, nullptr) < 0) {
    avcodec_free_context(&codec);
    errorCode = MediaError::decodeFailed;
    errorMessage = "video decoder open failed";
    return false;
  }
  codec->pkt_timebase = stream->time_base;
  const int32_t rotation = streamRotation(stream);
  const int32_t dvProfile = streamDvProfile(stream);
  const int64_t origin =
      stream->start_time != AV_NOPTS_VALUE ? stream->start_time : 0;
  int64_t durationMs = sourceInfo ? sourceInfo->durationMs() : 0;
  int64_t targetMs = request.timeMs > 0 ? request.timeMs : 0;
  if (durationMs > 0 && targetMs > durationMs - 1) {
    targetMs = durationMs - 1;
  }
  const int64_t targetTs =
      origin + av_rescale_q(targetMs, AVRational{1, 1000}, stream->time_base);
  const int seekRet =
      av_seek_frame(fmtCtx, videoIndex, targetTs, AVSEEK_FLAG_BACKWARD);
  if (seekRet < 0) {
    avcodec_free_context(&codec);
    errorCode = bCancel ? MediaError::cancelled : MediaError::seekFailed;
    errorMessage = bCancel ? "cancelled" : ffmpegErrMsg(seekRet);
    return false;
  }
  avcodec_flush_buffers(codec);
  // 每帧请求复位元数据: 非 DV 片不能沿用上个 DV 片的整形链(会整体偏色)
  HdrMeta hdrMeta = {};
  DoviMeta doviMeta = {};
  if (!thumbRenderer) {
    thumbRenderer = std::make_unique<MediaThumbRenderer>();
  }
  thumbRenderer->setHdrMeta(hdrMeta);
  thumbRenderer->setDoviMeta(doviMeta);
  MetaExtractor::Callbacks cb;
  cb.onHdrMeta = [this](const HdrMeta& meta) {
    if (thumbRenderer) {
      thumbRenderer->setHdrMeta(meta);
    }
  };
  cb.onDoviMeta = [this](const DoviMeta& meta) {
    if (thumbRenderer) {
      thumbRenderer->setDoviMeta(meta);
    }
  };
  MetaExtractor meta(std::move(cb));
  meta.setCodec(ffVCodec(stream->codecpar->codec_id));
  meta.setDvProfile(dvProfile);
  meta.reset();

  AVPacket* packet = av_packet_alloc();
  AVFrame* frame = av_frame_alloc();
  AVFrame* chosen = av_frame_alloc();
  bool bAvccKnown = false;
  int32_t decoded = 0;
  int64_t chosenMs = targetMs;
  bool got = false;
  const auto started = std::chrono::steady_clock::now();
  while (!got && packet && frame && chosen && decoded < 600) {
    if (bCancel) {
      break;
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - started)
                             .count();
    if (elapsed > 4000) {
      break;
    }
    const int readRet = av_read_frame(fmtCtx, packet);
    if (readRet < 0) {
      break;  // EOF: 用已解出的最后一帧
    }
    if (packet->stream_index != videoIndex) {
      av_packet_unref(packet);
      continue;
    }
    if (!bAvccKnown) {
      meta.setAvcc(checkAvccPacket(packet->data, packet->size));
      bAvccKnown = true;
    }
    AvoxPacket avoxPacket = {};
    avoxPacket.packtype = (int32_t)PackType::video;
    avoxPacket.index = videoIndex;
    avoxPacket.frameType = (packet->flags & AV_PKT_FLAG_KEY) ? 1 : 0;
    avoxPacket.pts = packet->pts;
    avoxPacket.dts = packet->dts;
    avoxPacket.data.data = packet->data;
    avoxPacket.data.size = packet->size;
    avoxPacket.data.bRef = 1;
    meta.extract(avoxPacket);
    const bool bSend = avcodec_send_packet(codec, packet) >= 0;
    av_packet_unref(packet);
    if (!bSend) {
      continue;
    }
    while (avcodec_receive_frame(codec, frame) >= 0) {
      ++decoded;
      const int64_t pts = frame->best_effort_timestamp != AV_NOPTS_VALUE
                              ? frame->best_effort_timestamp
                              : frame->pts;
      int64_t ms = ptsToMs(pts, stream->time_base, origin);
      if (ms < 0) {
        ms = targetMs;
      }
      av_frame_unref(chosen);
      av_frame_ref(chosen, frame);
      chosenMs = ms;
      got = ms >= targetMs;
      av_frame_unref(frame);
      if (got) {
        break;
      }
    }
  }
  const bool bCancelled = bCancel.load();
  if (bCancelled) {
    errorCode = MediaError::cancelled;
    errorMessage = "cancelled";
  }
  bool ok = false;
  if (!bCancelled) {
    if (chosen && chosen->data[0]) {
      const YuvType yuvType = ffYuvType((AVPixelFormat)chosen->format);
      if (!MediaThumbRenderer::supportsYuv(yuvType)) {
        const char* name =
            av_get_pix_fmt_name((AVPixelFormat)chosen->format);
        errorCode = MediaError::unsupportedPixFmt;
        errorMessage = name ? name : "unknown";
      } else if (!thumbRenderer->prepare(request.width, chosen->width,
                                         chosen->height, rotation, yuvType,
                                         frameColorSpace(chosen))) {
        errorCode = MediaError::renderUnavailable;
        errorMessage = "vulkan thumbnail graph is unavailable";
      } else {
        YUVFrame yuv = {};
        yuv.pts = chosenMs;
        yuv.format.width = chosen->width;
        yuv.format.height = chosen->height;
        yuv.format.type = yuvType;
        for (int i = 0; i < 3 && i < AV_NUM_DATA_POINTERS; ++i) {
          yuv.data[i] = chosen->data[i];
          yuv.stride[i] = chosen->linesize[i];
        }
        if (thumbRenderer->render(yuv)) {
          IImageBuffer* image = thumbRenderer->image();
          const ImageFormat format = image->getImageFormat();
          MediaFrameInfo info = {};
          info.requestedMs = request.timeMs;
          info.actualMs = chosenMs;
          info.width = format.width;
          info.height = format.height;
          if (observer) {
            observer->onFrame(info, image);
          }
          ok = true;
        } else {
          errorCode = MediaError::renderUnavailable;
          errorMessage = "vulkan thumbnail render failed";
        }
      }
    } else if (decoded <= 0) {
      errorCode = MediaError::decodeFailed;
      errorMessage = "no frame decoded from target position";
    } else {
      errorCode = MediaError::timeout;
      errorMessage = "frame decode budget exhausted";
    }
  }
  if (frame) {
    av_frame_free(&frame);
  }
  if (chosen) {
    av_frame_free(&chosen);
  }
  if (packet) {
    av_packet_free(&packet);
  }
  avcodec_free_context(&codec);
  if (!ok && errorCode == MediaError::none) {
    errorCode = MediaError::other;
    errorMessage = "frame grab failed";
  }
  return ok;
}

void MediaReaderFF::cancel() { bCancel = true; }

void MediaReaderFF::close() {
  if (state == MediaReaderState::idle) {
    return;  // idle 时 no-op
  }
  const MediaReaderState from = state;
  bCancel = true;  // 契约上应无在飞操作; 防御性打断
  // 先收抓帧渲染器(disableImage 需要 VK 图还活着), 再放容器上下文
  thumbRenderer.reset();
  closeInput();
  sourceInfo.reset();
  indexPeaks.clear();
  indexKeyframes.clear();
  errorCode = MediaError::none;
  errorMessage.clear();
  notifyState(from, MediaReaderState::closing);
  if (observer) {
    observer->onClose();
  }
  notifyState(MediaReaderState::closing, MediaReaderState::idle);
}

void MediaReaderFF::closeInput() {
  if (fmtCtx) {
    AVFormatContext* ctx = fmtCtx;
    fmtCtx = nullptr;
    avformat_close_input(&ctx);
  }
}

ISourceInfo* MediaReaderFF::getSourceInfo() { return sourceInfo.get(); }

MediaError MediaReaderFF::lastError() { return errorCode; }

const char* MediaReaderFF::lastErrorMessage() { return errorMessage.c_str(); }

IMediaReader* createMediaReader() {
  return new (std::nothrow) MediaReaderFF();
}

}  // namespace avox
