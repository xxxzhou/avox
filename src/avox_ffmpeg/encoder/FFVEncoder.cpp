#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include "FFVEncoder.hpp"

#include "avox/muxer/Muxer.hpp"

namespace avox {

namespace {

// Windows 平台硬编探测表(名字都取不到时的兜底; 顺序 = 压缩效率优先)
struct HWEncoderInfo {
  AVHWDeviceType type;
  const char* h264;
  const char* h265;
};

// 编码器名子串判定: FFmpeg 编码器名都带实现后缀(h264_mf/hevc_nvenc/h264_vaapi)
bool nameHas(const char* name, const char* sub) {
  return name && sub && strstr(name, sub) != nullptr;
}

// avox 量程 -> FFmpeg, 与 shader 矩阵(VideoDesc.colorSpace)同源
AVColorRange mapColorRange(YuvRange r) {
  return r == YuvRange::limited ? AVCOL_RANGE_MPEG : AVCOL_RANGE_JPEG;
}

// avox 规范 -> FFmpeg
AVColorSpace mapColorSpace(YuvStandard s) {
  if (s == YuvStandard::bt709) {
    return AVCOL_SPC_BT709;
  }
  if (s == YuvStandard::bt2020) {
    return AVCOL_SPC_BT2020_NCL;
  }
  return AVCOL_SPC_SMPTE170M;  // BT601(FFmpeg 无 AVCOL_SPC_BT601,用 SMPTE170M)
}

// 统一质量档(0..100, 越大越好) → x264 系 CRF 量纲(越小越好)。
// 60 → 23, 与 libx264 crf23(实测 VMAF 95.8)对齐; 100 → 16; 50 → 25
int32_t qualityToCrf(int32_t quality) {
  int32_t crf = 34 - quality * 18 / 100;
  if (crf < 16) {
    crf = 16;
  } else if (crf > 40) {
    crf = 40;
  }
  return crf;
}

// 开成功后仍留在字典里的键 = 该编码器不认的参数, 留个证据(选项名跨版本会漂移)
std::string leftKeys(AVDictionary* param) {
  std::string keys;
  const AVDictionaryEntry* e = nullptr;
  while ((e = av_dict_get(param, "", e, AV_DICT_IGNORE_SUFFIX)) != nullptr) {
    if (!keys.empty()) {
      keys += ",";
    }
    keys += e->key;
  }
  return keys;
}

// ---- 可移植旋钮 → 各家原生参数 ----
// 预设三档(速度↔质量)。各编码器原生档数/档名都不同, 只做映射; MF 与
// MediaCodec 没有预设档, 不下发(见 doc_record.md 的平台支持注)
const char* presetX26x(EncodePreset p) {
  // 实测 veryfast 与 medium 体积基本同档(824 与 820 kbps), 均衡档取 veryfast
  switch (p) {
    case EncodePreset::speed:
      return "superfast";
    case EncodePreset::quality:
      return "medium";
    default:
      return "veryfast";
  }
}

const char* presetNvenc(EncodePreset p) {
  switch (p) {
    case EncodePreset::speed:
      return "p4";
    case EncodePreset::quality:
      return "p7";
    default:
      return "p6";
  }
}

const char* presetQsv(EncodePreset p) {
  switch (p) {
    case EncodePreset::speed:
      return "veryfast";
    case EncodePreset::quality:
      return "slow";
    default:
      return "medium";
  }
}

const char* presetAmf(EncodePreset p) {
  switch (p) {
    case EncodePreset::speed:
      return "speed";
    case EncodePreset::quality:
      return "high_quality";
    default:
      return "quality";
  }
}

// VAAPI 的 quality 语义与其余相反(越高越快), iHD/i965 都是 1..7
int32_t presetVaapi(EncodePreset p) {
  switch (p) {
    case EncodePreset::speed:
      return 7;
    case EncodePreset::quality:
      return 1;
    default:
      return 4;
  }
}

// profile: HEVC 三档都落 main(输入 8bit, Main10 不适用); AVC 由调用方给三档名
const char* profileAvc(EncodeProfile p, const char* compat,
                       const char* main, const char* high) {
  switch (p) {
    case EncodeProfile::compat:
      return compat;
    case EncodeProfile::main:
      return main;
    default:
      return high;
  }
}

}  // namespace

FFVEncoder::FFVEncoder() {}

FFVEncoder::~FFVEncoder() {}

// 按编码器名分发参数。各家的码控词汇/量纲互不相同, 统一的是三个旋钮的语义:
// 码控模式(质量优先 / VBR / CBR) × 质量档或码率, 加上 profile 与预设两档。
// 质量定档一律把 codecCtx->bit_rate 归零: 留着它会与质量目标并发, 把画质拖下来
// tier: 0=全量参数, 1=去掉激进项(B 帧/前瞻/预设/scenario), 见 onPreEncoder
void FFVEncoder::applyEncoderParams(AVCodecContext* ctx, const AVCodec* codec,
                                    int32_t bitrate, int32_t quality,
                                    int32_t tier, AVDictionary** param) {
  const EncodeRateMode rcMode = getRateMode();
  const bool bQuality = rcMode == EncodeRateMode::quality;
  const bool bCbr = rcMode == EncodeRateMode::cbr;
  const EncodePreset preset = getPreset();
  const EncodeProfile profile = getProfileLevel();
  const bool bH265 = desc.codecId == VCodecId::h265;
  const char* name = codec->name ? codec->name : "";
  const int32_t crf = qualityToCrf(quality) + (bH265 ? 5 : 0);
  const bool bFull = tier == 0;
  // B 帧只在非推流且最低一级没被砍掉时开(fillContext 按同一规则设 ctx)
  const bool bAllowB = bFull && usage == EncodeUsage::record;

  if (nameHas(name, "libx264") || nameHas(name, "libx265")) {
    // 软编(GPL 渠道)。实测同画质下 veryfast 与 medium 体积基本同档(824 与
    // 820 kbps @VMAF95.8), 而旧的 ultrafast 是 6983 kbps —— CABAC/去块/
    // 子像素全被关掉, 8.5 倍体积
    if (bFull) {
      av_dict_set(param, "preset", presetX26x(preset), 0);
    }
    av_dict_set(param, "profile",
                bH265 ? "main"
                      : profileAvc(profile, "baseline", "main", "high"),
                0);
    if (bQuality) {
      av_dict_set_int(param, "crf", crf, 0);
      ctx->bit_rate = 0;
    } else {
      ctx->bit_rate = bitrate;
      if (bCbr) {
        // x264 系没有独立 CBR 开关: 三段钳到同一值 + 1 秒缓冲即严格 CBR
        ctx->rc_min_rate = bitrate;
        ctx->rc_max_rate = bitrate;
        ctx->rc_buffer_size = (int32_t)bitrate;
      }
    }
    if (usage == EncodeUsage::stream) {
      av_dict_set(param, "tune", "zerolatency", 0);
    }
    return;
  }
  if (nameHas(name, "_mf")) {
    // MediaFoundation: Windows 商业渠道唯一在编的硬编(h264_mf/hevc_mf)。
    // 实测质量档比码率档更省(同画质 2602 与 3665 kbps), 且质量档下 bit_rate
    // 被忽略(实测同尺寸); quality 量纲 0~100 越大越好, 与统一档同向直接给。
    // 无预设档, 也吃不下字符串 profile(其 profile 是数值枚举), 两者都不下发
    if (bQuality) {
      av_dict_set(param, "rate_control", "quality", 0);
      av_dict_set_int(param, "quality", quality, 0);
      if (bFull) {
        av_dict_set(param, "scenario", "archive", 0);
      }
      ctx->bit_rate = 0;
    } else {
      av_dict_set(param, "rate_control", bCbr ? "cbr" : "u_vbr", 0);
      if (bFull) {
        av_dict_set(param, "scenario", bCbr ? "live_streaming" : "archive", 0);
      }
      ctx->bit_rate = bitrate;
    }
    return;
  }
  if (nameHas(name, "_nvenc")) {
    if (bFull) {
      av_dict_set(param, "preset", presetNvenc(preset), 0);
    }
    av_dict_set(param, "profile",
                bH265 ? "main"
                      : profileAvc(profile, "baseline", "main", "high"),
                0);
    av_dict_set(param, "tune", usage == EncodeUsage::stream ? "ll"
                                                                          : "hq",
                0);
    if (bQuality) {
      // VBR + cq = NVENC 的质量定档; vbr 下 bit_rate 归零才是纯质量档
      av_dict_set(param, "rc", "vbr", 0);
      av_dict_set_int(param, "cq", crf + 2, 0);
      if (bFull) {
        av_dict_set_int(param, "rc-lookahead", 32, 0);
        av_dict_set_int(param, "spatial-aq", 1, 0);
        av_dict_set_int(param, "temporal-aq", 1, 0);
      }
      ctx->bit_rate = 0;
    } else {
      av_dict_set(param, "rc", bCbr ? "cbr" : "vbr", 0);
      ctx->bit_rate = bitrate;
      if (bCbr) {
        av_dict_set_int(param, "rc-lookahead", 0, 0);
      }
    }
    if (bAllowB) {
      av_dict_set_int(param, "bf", 3, 0);
    }
    return;
  }
  if (nameHas(name, "_qsv")) {
    if (bFull) {
      av_dict_set(param, "preset", presetQsv(preset), 0);
    }
    av_dict_set(param, "profile",
                bH265 ? "main"
                      : profileAvc(profile, "baseline", "main", "high"),
                0);
    if (bQuality) {
      // ICQ: 画质走通用的 global_quality(与 CLI -global_quality 同口径), 1~51
      // 越小越好; 只在最低一级下发 —— 部分驱动的 ICQ 与前瞻/B 帧组合会开失败
      ctx->global_quality = crf;
      ctx->bit_rate = 0;
      if (bFull) {
        av_dict_set_int(param, "look_ahead", 1, 0);
        av_dict_set_int(param, "look_ahead_depth", 40, 0);
      }
    } else {
      ctx->bit_rate = bitrate;
      if (bCbr) {
        ctx->rc_max_rate = bitrate;
        ctx->rc_buffer_size = (int32_t)bitrate;
      } else if (bFull) {
        av_dict_set_int(param, "look_ahead", 1, 0);
        av_dict_set_int(param, "look_ahead_depth", 40, 0);
        av_dict_set_int(param, "extbrc", 1, 0);
      }
    }
    return;
  }
  if (nameHas(name, "_amf")) {
    // AMF 实测(本机 RX9070XT): cqp 24/26/28 → 1087kbps@VMAF97.0, 优于 x264
    // crf23 的 820@95.8。质量档用 cqp(量化参数量纲唯一无歧义), 不用 qvbr
    // (实测其 quality_level 大小与体积同向, 与 AMD 文档描述的相反, 语义存疑)
    if (bFull) {
      av_dict_set(param, "quality", presetAmf(preset), 0);
    }
    av_dict_set(param, "profile",
                bH265 ? "main"
                      : profileAvc(profile, "constrained_baseline", "main",
                                   "high"),
                0);
    if (bQuality) {
      av_dict_set(param, "usage", "high_quality", 0);
      av_dict_set(param, "rc", "cqp", 0);
      av_dict_set_int(param, "qp_i", crf + 1, 0);
      av_dict_set_int(param, "qp_p", crf + 3, 0);
      if (bFull) {
        av_dict_set_int(param, "qp_b", crf + 5, 0);
      }
      ctx->bit_rate = 0;
    } else {
      av_dict_set(param, "usage",
                  usage == EncodeUsage::stream ? "lowlatency"
                                                             : "transcoding",
                  0);
      av_dict_set(param, "rc", bCbr ? "cbr" : "vbr_peak", 0);
      ctx->bit_rate = bitrate;
    }
    return;
  }
  if (nameHas(name, "_vaapi")) {
    // VAAPI(Linux 硬编): rc_mode/qp/profile/quality 都是 vaapi 私有选项
    av_dict_set(param, "profile",
                bH265 ? "main"
                      : profileAvc(profile, "constrained_baseline", "main",
                                   "high"),
                0);
    if (bQuality) {
      av_dict_set(param, "rc_mode", "CQP", 0);
      av_dict_set_int(param, "qp", crf + 2, 0);
      if (bAllowB) {
        av_dict_set_int(param, "b_depth", 3, 0);
      }
      ctx->bit_rate = 0;
    } else {
      av_dict_set(param, "rc_mode", bCbr ? "CBR" : "VBR", 0);
      ctx->bit_rate = bitrate;
      if (bCbr) {
        ctx->rc_max_rate = bitrate;
        ctx->rc_buffer_size = (int32_t)bitrate;
      }
    }
    if (bFull) {
      av_dict_set_int(param, "quality", presetVaapi(preset), 0);
    }
    return;
  }
  if (nameHas(name, "videotoolbox")) {
    // Apple 的 FFmpeg 兜底车道(默认车道是原生 IOSVEncoder, 见 IOSVEncoder.mm)
    av_dict_set(param, "profile",
                bH265 ? "main"
                      : profileAvc(profile, "baseline", "main", "high"),
                0);
    av_dict_set_int(param, "realtime",
                    usage == EncodeUsage::record ? 0 : 1, 0);
    if (bFull && preset != EncodePreset::balanced) {
      av_dict_set_int(
          param, "prio_speed",
          preset == EncodePreset::speed ? 1 : 0, 0);
    }
    if (bQuality) {
      // 画质档走 FFmpeg 通用 quality 字段(与 -q:v 同口径); 该封装无 ICQ 类模式
      ctx->global_quality = crf;
      ctx->bit_rate = 0;
    } else {
      ctx->bit_rate = bitrate;
    }
    return;
  }
  // 其余(如 h264_d3d12va): 质量档走通用 quality 字段, 码率档走码率。
  // 不下发 profile/预设 —— 部分实现的 profile 是数值枚举, 给字符串会 EINVAL
  if (bQuality) {
    ctx->global_quality = crf;
    ctx->bit_rate = 0;
  } else {
    ctx->bit_rate = bitrate;
    if (bCbr) {
      ctx->rc_max_rate = bitrate;
      ctx->rc_buffer_size = (int32_t)bitrate;
    }
  }
}

// 挑编码器。优先级 = 候选链选中名 → 渠道注入默认名 → (Windows)平台硬编探测
// → 该编码的第一个实现。让候选链的名字生效是这里的关键: 上层选软/硬的意图
// 在候选链里已经表达过, 编码器再"见 nv12 就换硬编"会把它覆盖掉
const AVCodec* FFVEncoder::selectCodec() {
  if (!encoderName.empty()) {
    const AVCodec* c = avcodec_find_encoder_by_name(encoderName.c_str());
    if (c) {
      return c;
    }
    LOGFLF(LogLevel::warn, "lane encoder not found:", encoderName);
  }
  const char* softName =
      (desc.codecId == VCodecId::h265) ? AVOX_FF_H265_ENCODER : AVOX_FF_H264_ENCODER;
  if (softName && *softName) {
    const AVCodec* c = avcodec_find_encoder_by_name(softName);
    if (c) {
      return c;
    }
  }
#ifdef _WIN32
  if (yuvType == YuvType::nv12) {
    static const HWEncoderInfo hwEncoders[] = {
        {AV_HWDEVICE_TYPE_QSV, "h264_qsv", "hevc_qsv"},
        {AV_HWDEVICE_TYPE_AMF, "h264_amf", "hevc_amf"},
        {AV_HWDEVICE_TYPE_CUDA, "h264_nvenc", "hevc_nvenc"}};
    for (const auto& enc : hwEncoders) {
      AVBufferRef* hwDeviceCtx = nullptr;
      if (av_hwdevice_ctx_create(&hwDeviceCtx, enc.type, nullptr, nullptr, 0) !=
          0) {
        continue;
      }
      av_buffer_unref(&hwDeviceCtx);
      const char* name = (desc.codecId == VCodecId::h265) ? enc.h265 : enc.h264;
      const AVCodec* c = avcodec_find_encoder_by_name(name);
      if (c) {
        return c;
      }
    }
  }
#endif
  return avcodec_find_encoder(getFFCodecId(desc.codecId));
}

// 建上下文(每级重试都重建: 上一级可能已把 ctx 改到半开状态)
void FFVEncoder::fillContext(const AVCodec* codec, const FrameRate& framerate,
                             int32_t bitrate, int32_t tier) {
  bCtxOpened = false;
  codecCtx = getUniquePtr(avcodec_alloc_context3(codec));
  codecCtx->codec_type = AVMEDIA_TYPE_VIDEO;
  codecCtx->pix_fmt = getFFVideoFormat(desc.desc.type);
  // 颜色标签: 与 rgba2yuv shader 的输出(由 VideoDesc.colorSpace 驱动)对齐,
  // 否则 VUI unspecified, 播放器默认按 limited 解释 -> 饱和色裁切偏色
  codecCtx->color_range = mapColorRange(desc.desc.colorSpace.range);
  codecCtx->colorspace = mapColorSpace(desc.desc.colorSpace.standard);
  codecCtx->color_primaries = AVCOL_PRI_BT709;
  codecCtx->color_trc = AVCOL_TRC_BT709;
  codecCtx->width = desc.desc.width;
  codecCtx->height = desc.desc.height;
  YUVFormat yuvFormat = {desc.desc.width, desc.desc.height, desc.desc.type};
  codecCtx->frame_size = getYuvFrameSize(yuvFormat, 0);
  codecCtx->framerate = av_make_q(framerate.numerator, framerate.denominator);
  // 时间戳按毫秒, 取当前时间戳, fps 不对也没关系
  codecCtx->time_base = {1, 1000};
  // 秒数 → 帧数的 GOP(与 VideoStream::gop 同口径)
  codecCtx->gop_size = gop * framerate.asDecimal();
  // B 帧: 录制允许重排(三个 muxer 都按 dts 落盘: FFmpeg 走 ctts, ZLM 收 dts/pts,
  // 播放侧引擎本就吃 B 帧流), 推流必须关(WebRTC 不接受重排)。只在最低一级
  // 重试里保留 —— 驱动不支持 B 帧时先掉它, 保住码控
  codecCtx->max_b_frames =
      (tier == 0 && usage == EncodeUsage::record) ? 3 : 0;
  codecCtx->has_b_frames = codecCtx->max_b_frames;
  codecCtx->bit_rate = bitrate;
  codecCtx->thread_count = 0;
  codecCtx->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;
}

DecodeResult FFVEncoder::onPreEncoder() {
  const AVCodec* codec = selectCodec();
  if (!codec) {
    LOGFLF(LogLevel::info, "avcodec_find_encoder failed,codecId:",
           getVCodecName(desc.codecId));
    return DecodeResult::openFailed;
  }
  FrameRate framerate = {30, 1};
  if (desc.desc.fps > 0) {
    framerate.build(desc.desc.fps);
  }
  const int32_t bitrate = getTargetBitrate();
  const int32_t quality = getQualityLevel();
  // 分级下发: 参数在某编码器/驱动上不被接受(选项名跨版本漂移、能力缺失)时逐级
  // 降档(0=全量 → 1=只留档位与码控 → 2=厂家默认), 让"参数猜错"退化成旧行为,
  // 而不是把整条录制链开不出来
  for (int32_t tier = 0; tier < 3; ++tier) {
    AVDictionary* param = nullptr;
    fillContext(codec, framerate, bitrate, tier);
    if (tier < 2) {
      applyEncoderParams(codecCtx.get(), codec, bitrate, quality, tier, &param);
    }
    onAttachContext();
    int32_t ret = avcodec_open2(codecCtx.get(), codec, &param);
    if (ret >= 0) {
      std::string left = leftKeys(param);
      av_dict_free(&param);
      bCtxOpened = true;
      frame = getUniquePtr(av_frame_alloc());
      frameCount = 0;
      LOGFLF(LogLevel::info, "video encoder open:", codec->name, " tier:", tier,
             " usage:",
             usage == EncodeUsage::stream ? "stream" : "record",
             " rateMode:", (int32_t)getRateMode(), " bitrate:", bitrate,
             " quality:", quality, " preset:", (int32_t)getPreset(),
             " profile:", (int32_t)getProfileLevel(), " gop:", gop);
      if (!left.empty()) {
        LOGFLF(LogLevel::warn, "encoder ignored params:", left);
      }
      return DecodeResult::success;
    }
    av_dict_free(&param);
    AVOX_FFMEPG_LOG(ret, "avcodec_open2 failed");
  }
  return DecodeResult::openFailed;
}

DecodeResult FFVEncoder::encode(const YUVFrame& yframe) {
  // [dbg] ENH_VKDBG=1: 编码器入口
  {
    static const bool dbg = std::getenv("ENH_VKDBG") != nullptr;
    static std::atomic<int32_t> cnt{0};
    if (dbg && cnt.fetch_add(1) < 3) {
      fprintf(stderr, "[dbg] FFVEncoder encode entry, %ux%u pts=%lld\n",
              (unsigned)yframe.format.width, (unsigned)yframe.format.height,
              (long long)yframe.pts);
    }
  }
  // 检查分辨率是否变化,如果变化,编码器重置
  bool bChange = desc.desc.width != yframe.format.width ||
                 desc.desc.height != yframe.format.height;
  if (!codecCtx || !frame || bChange) {
    if (bChange) {
      LOGFLF(LogLevel::info, "old size width:", desc.desc.width,
             " height:", desc.desc.height,
             " change size width:", yframe.format.width,
             " height:", yframe.format.height);
    }
    yuvType = yframe.format.type;
    desc.desc.width = yframe.format.width;
    desc.desc.height = yframe.format.height;
    DecodeResult result = onPreEncoder();
    if (result != DecodeResult::success) {
      return result;
    }
    // 分析配置帧，并发送配置信息
  }
  frame->format = codecCtx->pix_fmt;
  frame->width = yframe.format.width;
  frame->height = yframe.format.height;
  frame->data[0] = yframe.data[0];
  frame->data[1] = yframe.data[1];
  frame->data[2] = yframe.data[2];
  frame->linesize[0] = yframe.stride[0];
  frame->linesize[1] = yframe.stride[1];
  frame->linesize[2] = yframe.stride[2];
  // frame->pts = frameCount++;
  frame->pts = yframe.pts;
  // 上层点名要关键帧(WebRTC 丢包后请求 IDR): 不置 pict_type 就只能等下一个
  // GOP, 期间画面花屏
  if (yframe.keyFrame) {
    frame->pict_type = AV_PICTURE_TYPE_I;
  }
  // LOGFLF(LogLevel::info, "encode video pts:", frame->pts);
  int32_t ret = avcodec_send_frame(codecCtx.get(), frame.get());
  if (ret >= 0) {
    ++sentFrames;
  }
  // [dbg] ENH_VKDBG=1: send/receive 结果(定位包不出编码器的环节)
  {
    static const bool dbg = std::getenv("ENH_VKDBG") != nullptr;
    static std::atomic<int32_t> cnt{0};
    if (dbg && cnt.fetch_add(1) < 5) {
      fprintf(stderr, "[dbg] FFVEncoder send_frame ret=%d\n", ret);
    }
  }
  if (ret < 0) {
    if (ret == AVERROR_EOF) {
      return DecodeResult::complete;
    }
    if (ret == AVERROR(EAGAIN)) {
      return DecodeResult::dataNoReady;
    } else {
      AVOX_FFMEPG_LOG(ret, "avcodec_send_frame failed");
      return DecodeResult::dataError;
    }
  }
  while (true) {
    AVPacket* packet = av_packet_alloc();
    ret = avcodec_receive_packet(codecCtx.get(), packet);
    // [dbg] ENH_VKDBG=1: receive 结果计数(前20次含ret码)
    {
      static const bool dbg = std::getenv("ENH_VKDBG") != nullptr;
      static std::atomic<int32_t> cnt{0};
      if (dbg) {
        int32_t c = cnt.fetch_add(1);
        if (c < 20) {
          fprintf(stderr, "[dbg] FFVEncoder receive #%d ret=%d\n", c, ret);
        }
      }
    }
    if (ret < 0) {
      if (ret == AVERROR_EOF) {
        av_packet_free(&packet);
        return DecodeResult::complete;
      }
      if (ret == AVERROR(EAGAIN)) {
        av_packet_free(&packet);
        break;
      }
      av_packet_free(&packet);
      AVOX_FFMEPG_LOG(ret, "avcodec_receive_packet failed");
      return DecodeResult::dataError;
    }
    // android平台发现有时候会返回空包
    if (packet->size <= 4) {
      av_packet_free(&packet);
      continue;
    }
    // 时间戳转为毫秒 time_base本身是ms可以不需要
    av_packet_rescale_ts(packet, codecCtx->time_base, {1, 1000});
    AvoxPacket cpacket = ffAvoxPacket(packet);
    cpacket.packtype = (int32_t)PackType::video;
    // [dbg] ENH_VKDBG=1: 编码器出包
    {
      static const bool dbg = std::getenv("ENH_VKDBG") != nullptr;
      static std::atomic<int32_t> cnt{0};
      if (dbg && cnt.fetch_add(1) < 5) {
        fprintf(stderr, "[dbg] FFVEncoder packet out, size=%u pts=%lld "
                        "keyflag=%d\n",
                (unsigned)cpacket.data.size, (long long)cpacket.pts,
                (int)cpacket.frameType);
      }
    }
    dispatch(&IEncoderOb::onPacket, cpacket);
    // free 而非 unref: 包结构本身也是每次 alloc 出来的(旧写法只 unref, 每帧漏一个)
    av_packet_free(&packet);
  }
  return DecodeResult::success;
}

void FFVEncoder::flush() {
  // 未open的ctx(alloc到open2之间/open2失败残留)internal为空, flush必崩
  if (codecCtx && bCtxOpened) {
    avcodec_flush_buffers(codecCtx.get());
  }
}

void FFVEncoder::drain() {
  if (!codecCtx || !bCtxOpened) {
    return;
  }
  // send NULL 让编码器把内部缓冲吐完。不能用 avcodec_flush_buffers 顶替:
  // 那是"丢弃缓冲并复位" —— 实测 h264_mf 质量档前瞻积了 33 帧(1.1 秒),
  // 停流时丢弃就是产物尾部静默少一截
  int32_t ret = avcodec_send_frame(codecCtx.get(), nullptr);
  if (ret < 0 && ret != AVERROR_EOF) {
    AVOX_FFMEPG_LOG(ret, "avcodec_send_frame null failed");
    return;
  }
  // 硬件 MFT 是异步的: send NULL 之后输出不是立刻可取, receive 先返 EAGAIN,
  // 要轮询等到吐完(软件编码器会一路吐到 EOF)。给个上限, 不能死等
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(2);
  int32_t drained = 0;
  while (true) {
    AVPacket* packet = av_packet_alloc();
    ret = avcodec_receive_packet(codecCtx.get(), packet);
    if (ret == AVERROR(EAGAIN)) {
      av_packet_free(&packet);
      if (std::chrono::steady_clock::now() >= deadline) {
        LOGFLF(LogLevel::warn, "encoder drain wait timeout, tail may lost");
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      continue;
    }
    if (ret < 0) {
      av_packet_free(&packet);
      break;  // AVERROR_EOF = 吐完(或真错误)
    }
    if (packet->size <= 4) {
      av_packet_free(&packet);
      continue;
    }
    av_packet_rescale_ts(packet, codecCtx->time_base, {1, 1000});
    AvoxPacket cpacket = ffAvoxPacket(packet);
    cpacket.packtype = (int32_t)PackType::video;
    dispatch(&IEncoderOb::onPacket, cpacket);
    ++drained;
    av_packet_free(&packet);
  }
  // 尾帧缺失的诊断锚点: sent(喂进多少) vs drained(临停时又吐出多少)
  LOGFLF(LogLevel::info, "video encoder drain, sent:", sentFrames,
         " drained:", drained, " codec:", codecCtx->codec->name);
}

}
