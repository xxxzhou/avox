#include "AndVDecoder.hpp"

#include <cstdio>
#include <cstdlib>
#include <dlfcn.h>
#include <strings.h>

#include "avox/AvoxTime.h"
#include "avox/codec/H26XHelper.hpp"
#include "avox/module/AvoxManager.hpp"
#include "avox/player/MediaPlayer.hpp"
#include "avox_egl/EglWindow.hpp"

namespace avox {

#if __ANDROID_API__ >= 21

namespace {
// 诊断口(AVOX_ANDROID_PTS_LOG=1): 打投递序 in 与 MediaCodec 吐帧序 out 的 pts;
// 两者逐项相同 = 按解码序吐帧(本类缺重排), out 单调 = 已自行重排。真机一跑即定论
bool andPtsLog() {
  static const bool b = [] {
    const char* e = getenv("AVOX_ANDROID_PTS_LOG");
    return e && *e == '1';
  }();
  return b;
}

// 平台软实现命名(c2.android.vp9.decoder / OMX.google.*): 命中不算硬解命中,
// 由选型层回退 ffmpeg 软解; 厂商硬实现是 c2.qti.* / OMX.qcom.* 等
bool isMediaCodecSwName(const char* name) {
  if (!name) {
    return false;
  }
  return strncmp(name, "c2.android.", 11) == 0 ||
         strncasecmp(name, "omx.google.", 11) == 0 ||
         strstr(name, ".google.") != nullptr;
}

// 车道守卫(见 checkLaneAlive): 判据只用"健康车道不可能产生"的量。
// kLaneRefusedMax 是大数兜底 —— 该计数只随供给量增长(每喂一个包至多多 1),
// 要凑满它必须"一直喂、一直拒、且一个输出都没出", 健康车道在首帧前不可能满足;
// 再加 kLaneRefusedMinMs 把"供给极快"的短促突发也挡掉
constexpr int32_t kLaneRefusedMax = 1000;
constexpr int64_t kLaneRefusedMinMs = 1500;

// dequeue 硬错判定。依据 NDK 实现(NdkMediaCodec.cpp):
//   dequeueInputBuffer: status==OK 给 index, -EAGAIN 给 TRY_AGAIN_LATER(-1),
//     其余 status_t 才走 translate_error 变成 AMEDIA_ERROR_UNKNOWN(-10000)
//   dequeueOutputBuffer: 另加 FORMAT_CHANGED(-2)/BUFFERS_CHANGED(-3) 两个正常态
// ⇒ 健康车道"暂时没空位/暂时没输出"只可能是 -1/-2/-3(或旧版 WOULD_BLOCK);
// 任何其它负值都出自错误分支, 只有 codec 已不在可用态才会有
bool isDequeHardError(ssize_t v) {
  if (v >= 0) {
    return false;
  }
  return v != AMEDIACODEC_INFO_TRY_AGAIN_LATER &&
         v != AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED &&
         v != AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED &&
         v != AMEDIA_ERROR_WOULD_BLOCK;
}
}  // namespace

void regVDecoderReg() {
  RegFunc andVDecoderReg = {
      "android video decoder init", []() {
        VCodecDesc codecDesc = {};
        // 初始化 codecDesc 的相关信息，例如名称、是否支持硬件加速等 ;
        codecDesc.name = AVOX_ANDROID_H264_DECODER;
        codecDesc.bHardware = true;
        codecDesc.vcodecId = VCodecId::h264;
        AvoxManager::Get().vDecoders.regInitFunc(
            VCodecId::h264, codecDesc,
            []() -> VideoDecoder* { return new AndVDecoder(); });

        codecDesc = {};
        // 初始化 codecDesc 的相关信息，例如名称、是否支持硬件加速等 ;
        codecDesc.name = AVOX_ANDROID_H265_DECODER;
        codecDesc.bHardware = true;
        codecDesc.vcodecId = VCodecId::h265;
        AvoxManager::Get().vDecoders.regInitFunc(
            VCodecId::h265, codecDesc,
            []() -> VideoDecoder* { return new AndVDecoder(); });

        // VP9(webm): onVaild 按解码器名排除软实现, 无硬解的设备回退软解
        codecDesc = {};
        codecDesc.name = AVOX_ANDROID_VP9_DECODER;
        codecDesc.bHardware = true;
        codecDesc.vcodecId = VCodecId::vp9;
        AvoxManager::Get().vDecoders.regInitFunc(
            VCodecId::vp9, codecDesc,
            []() -> VideoDecoder* { return new AndVDecoder(); });

        // AV1: 同 VP9 口径, onVaild 排除平台软实现(c2.android.av1), 无硬解
        // 设备回退 FFmpeg 软解(dav1d 在场时归一名)
        codecDesc = {};
        codecDesc.name = AVOX_ANDROID_AV1_DECODER;
        codecDesc.bHardware = true;
        codecDesc.vcodecId = VCodecId::av1;
        AvoxManager::Get().vDecoders.regInitFunc(
            VCodecId::av1, codecDesc,
            []() -> VideoDecoder* { return new AndVDecoder(); });
      }};
  AvoxManager::Get().initFuncs.push_back(andVDecoderReg);
}

AndVDecoder::AndVDecoder() {
  codecTH = VCodecTh::androidMC;
  bMustAnnexb = true;
}

AndVDecoder::~AndVDecoder() { onClose(); }

bool AndVDecoder::createCodec() {
  VCodecId codecId = codecDesc.vcodecId;
  const char* mime = nullptr;
  switch (codecId) {
    case VCodecId::h264:
      // AMEDIAFORMAT_KEY_THUMBNAIL_CSD_AV1C
      mime = "video/avc";  // H264 MIME类型
      break;
    case VCodecId::h265:
      mime = "video/hevc";  // H265 MIME类型
      break;
    case VCodecId::vp9:
      mime = "video/x-vnd.on2.vp9";  // VP9 MIME类型
      break;
    case VCodecId::av1:
      mime = "video/av01";  // AV1 MIME类型
      break;
    default:
      LOGFLF(LogLevel::warn, "unsupported codec");
      return false;
  }
  // 先关闭可能存在的mediaCodec/format
  onClose();
  // AV1 是 OBU 自带长度字段的自成流, 无 avcc/hvcc 概念, 禁掉基类转换
  bMustAnnexb = codecDesc.vcodecId != VCodecId::av1;
  mediaCodec = AMediaCodec_createDecoderByType(mime);
  if (!mediaCodec) {
    return false;
  }
// VP9/AV1: createDecoderByType 在无硬解设备上会落平台软实现(c2.android.*),
// 按名字排除并回退软解。AMediaCodec_getName API 28+ 才有, 编译目标 26,
  if (codecId == VCodecId::vp9 || codecId == VCodecId::av1) {
    using GetNameFn = media_status_t (*)(AMediaCodec*, const char**);
    using FreeNameFn = void (*)(const char*);
    static GetNameFn getName =
        reinterpret_cast<GetNameFn>(dlsym(RTLD_DEFAULT, "AMediaCodec_getName"));
    static FreeNameFn freeName = reinterpret_cast<FreeNameFn>(
        dlsym(RTLD_DEFAULT, "AMediaCodec_freeName"));
    const char* mcName = nullptr;
    if (getName && getName(mediaCodec, &mcName) == AMEDIA_OK && mcName) {
      const bool bSw = isMediaCodecSwName(mcName);
      LOGFLF(LogLevel::info,
             codecId == VCodecId::av1 ? "av1 mediaCodec:" : "vp9 mediaCodec:",
             mcName, " hardware:", !bSw);
      if (freeName) {
        freeName(mcName);
      }
      if (bSw) {
        onClose();
        return false;
      }
    }
  }
  format = AMediaFormat_new();
  return true;
}

bool AndVDecoder::onVaild() { return createCodec(); }

DecodeResult AndVDecoder::onPreDecoder() {
// seek复位/bDecodeUpdate 会在 codec 仍处 Executing 态时重入本函数:
// MediaCodec 不允许运行中重配, 直接 configure 必吃 -10000(Java 层
  if (mediaCodec) {
    onClose();
    if (!createCodec()) {
      return DecodeResult::openFailed;
    }
  }
  auto packets = configPackets;
// csd 必须是AnnexB: mp4/rtmp 的 config 是avcc/hvcc长度前缀, 不转MediaCodec解析不了csd, 静默无输出
// 只转本地副本: 就地改 configPackets 会让换道后的 ffmpeg 按 Annex-B 建 extradata, 零帧
  if (codecDesc.vcodecId != VCodecId::av1) {
    for (auto& packet : packets) {
      AvoxPacket view = {};
      view.data = {packet.buff.data(), packet.size, true};
      avcc2AnnexbPacket(view);
    }
  }
  if (codecDesc.vcodecId == VCodecId::av1) {
// AV1: mp4 的 extradata 即 av1C 配置记录, 原样作 csd-0(MP4 demuxer 侧
// IOParseFF 对非 H26x 编码整段透传, 见 parseStream); mkv 的 CodecPrivate
    if (packets.empty()) {
      return DecodeResult::noConfig;
    }
    AMediaFormat_setString(format, "mime", "video/av01");
    AMediaFormat_setBuffer(format, "csd-0", packets.front().buff.data(),
                           packets.front().size);
  } else if (codecDesc.vcodecId == VCodecId::vp9) {
    // VP9: 无带外参数集(webm in-band), 不需要 csd; 无 csd 依赖的解析分支
    if (packets.empty()) {
      return DecodeResult::noConfig;
    }
    AMediaFormat_setString(format, "mime", "video/x-vnd.on2.vp9");
  } else if (codecDesc.vcodecId == VCodecId::h264) {
    if (packets.size() < 2) {
      return DecodeResult::noConfig;
    }
    bool has_sps = false, has_pps = false;
    for (auto& packet : packets) {
      // 获取NAL单元数据(跳过起始码)
      const uint8_t* nalu = packet.buff.data() + packet.prefixSize;
      uint8_t nalu_type = nalu[0] & 0x1F;  // H264类型在低5位

      if (nalu_type == (uint8_t)H264NAL::NAL_SPS) {
        AMediaFormat_setBuffer(format, "csd-0", packet.buff.data(),
                               packet.size);
        has_sps = true;
      } else if (nalu_type == (uint8_t)H264NAL::NAL_PPS) {
        AMediaFormat_setBuffer(format, "csd-1", packet.buff.data(),
                               packet.size);
        has_pps = true;
      }
    }
    if (!has_sps || !has_pps) {
      LOGFLF(LogLevel::warn, "missing SPS or PPS in H264 stream");
      return DecodeResult::noConfig;
    }
    AMediaFormat_setString(format, "mime", "video/avc");
  } else {
    if (packets.size() < 3) {
      return DecodeResult::noConfig;
    }
    // H265需要单独解析VPS/SPS/PPS
    bool has_vps = false, has_sps = false, has_pps = false;
    for (auto& packet : packets) {
      // 跳过起始码前缀(假设prefixSize包含起始码长度)
      const uint8_t* nalu = packet.buff.data() + packet.prefixSize;
      int nalu_size = packet.size - packet.prefixSize;
      // H265的NAL类型在第一个字节的2-7位
      uint8_t nalu_type = (nalu[0] >> 1) & 0x3F;
      switch (nalu_type) {
        case 32:  // VPS
          AMediaFormat_setBuffer(format, "csd-0", packet.buff.data(),
                                 packet.size);
          has_vps = true;
          break;
        case 33:  // SPS
          AMediaFormat_setBuffer(format, "csd-1", packet.buff.data(),
                                 packet.size);
          has_sps = true;
          break;
        case 34:  // PPS
          AMediaFormat_setBuffer(format, "csd-2", packet.buff.data(),
                                 packet.size);
          has_pps = true;
          break;
      }
    }
    if (!has_vps || !has_sps || !has_pps) {
      LOGFLF(LogLevel::warn, "missing VPS, SPS, or PPS in H265 stream");
      return DecodeResult::noConfig;
    }
    AMediaFormat_setString(format, "mime", "video/hevc");
  }
  bool bParse = parseConfigs();
  if (!bParse) {
    LOGFLF(LogLevel::warn,
           "parse configs failed: decoder parameters may contain incorrect "
           "information");
  }
  AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_WIDTH, params.width);
  AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_HEIGHT, params.height);
  // byte-buffer模式(console无JNI env,拿不到surface)请求semi-planar(0x15): QC硬解
  // 原生NV12直出, 避免vendor YV12转换且与yuvout车道的nv12契约一致; APK路径不变
  bool bHaveJni = AvoxManager::Get().getEnv() != nullptr;
  int32_t colorFmt = bHaveJni ? getYuvType(params.yuvType) : 0x15;
  if (!bHaveJni && params.yuvType == YuvType::p010) {
    // 10bit 流(byte-buffer): 按 vendor P010(0x36) 请求。钉成 0x15 会把 10bit
    // 交付降成 8bit NV12 → 下游 HDR/DV 消费链(VK V5 变体按输入格式选)整个
    // 失活, HDR 锚点直通/DV 差分恒 0 (2026-09-30 锚点假红根因链最后一环)。
    // vendor 不支持时 configure 失败, 上游回落 FFmpeg 软解(p010 原生交付)。
    colorFmt = COLOR_FormatYUVP010;
  }
  AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_COLOR_FORMAT, colorFmt);
  // 取证探针: 请求的输出色彩格式与流格式(交付降级时对读)
  fprintf(stderr, "[andvdec] request colorFmt=0x%x streamYuvType=%d jni=%d\n",
          colorFmt, (int)params.yuvType, (int)bHaveJni);
  bOpenglRender = false;
  // 输出窗口的渲染模式，检查是否支持opengl 纹理输入
  eglSize.width = params.width;
  eglSize.height = params.height;
  LOGFLF(LogLevel::info, "init egl context, width:", params.width,
         " height:", params.height);
  ANativeWindow* nativeWindow = nullptr;
  // 解码线程里的EGLContext为主，渲染的为辅，这样渲染的出了问题还能解码
  initContext(EGL_NO_CONTEXT);
  // 创建EGLSurface
  if (eglGetCurrentContext()) {
    // 渲染到OES纹理上
    surfaceTexture = std::make_unique<JniSurfaceTexture>();
    // 从GPU队列取出来的存放的OES纹理
    glGenTextures(1, &textureId);
    AVOX_GL_LOG("gen texture failed.");
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, textureId);
    glTexParameterf(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameterf(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_S,
                    GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_T,
                    GL_CLAMP_TO_EDGE);
    surfaceTexture->init(textureId);
    surfaceTexture->setImageSize(params.width, params.height);
    // std::function<void()> onAvailable = [&]() { bFrameAvailable = true; };
    // surfaceTexture->setOnFrameAvailableListener(onAvailable);
    nativeWindow = surfaceTexture->getNativeWindow();
    LOGFLF(LogLevel::info, "textureId:", textureId, " width:", params.width,
           " height:", params.height);
    // console进程无JNI env时surfaceTexture建不出来, nativeWindow为null,
    // MediaCodec实际落byte-buffer模式, 必须按CPU YUV帧分发, 否则渲染黑图
    bOpenglRender = nativeWindow != nullptr;
    if (!bOpenglRender) {
      LOGFLF(LogLevel::warn, "nativeWindow is null, decode to yuv buffers");
    }
  }
  LOGFLF(LogLevel::info, "bOpenglRender:", bOpenglRender);
  // 配置解码器
  media_status_t status =
      AMediaCodec_configure(mediaCodec, format, nativeWindow, nullptr, 0);
  if (status != AMEDIA_OK) {
    LOGFLF(LogLevel::warn, "configure failed:", status);
    return DecodeResult::openFailed;
  }
  // 启动解码器
  status = AMediaCodec_start(mediaCodec);
  if (status != AMEDIA_OK) {
    LOGFLF(LogLevel::warn, "start failed:", status);
    return DecodeResult::startFailed;
  }
  // 重置
  yuvFormat.width = 0;
  yuvFormat.height = 0;
  yuvFormat.type = params.yuvType;
  resetLaneCheck();
  bOpen = true;
  return DecodeResult::success;
}

void AndVDecoder::resetLaneCheck() {
  bLaneDead = false;
  bLaneOut = false;
  laneQueueFails = 0;
  laneDequeErrs = 0;
  laneRefused = 0;
  laneStartMs = 0;
  laneFedFrames = 0;
  bLaneErrLogged = false;
}

// 车道死即 openFailed: VDecoderTask 只在这个返回值上做换道, 交不出去才杀轨。
// 守卫只在首帧交付前生效(bLaneOut 后整体停用), 且只用两类判据:
//   A. 硬错误 —— 入队返回非 AMEDIA_OK、或 dequeue 返回非"稍后重试"的媒体错误码。
//      健康车道不会产生(拿到 index 就该能入队; 等空位只会得到 WOULD_BLOCK/-1)。
//   B. 大数兜底 —— 从未出图 + 被拒满 kLaneRefusedMax 次 + 已过 kLaneRefusedMinMs。
//      刻意不用"纯超时"判据: 首帧慢(高码率/高码流)与源侧饿等都会误伤
DecodeResult AndVDecoder::checkLaneAlive() {
  if (bLaneDead) {
    return DecodeResult::openFailed;
  }
  if (bLaneOut) {
    return DecodeResult::success;
  }
  if (laneStartMs == 0) {
    laneStartMs = timeStampMS();
  }
  if (laneRefused >= kLaneRefusedMax &&
      timeStampMS() - laneStartMs >= kLaneRefusedMinMs) {
    LOGFLF(LogLevel::warn, "android video decoder lane refused x", laneRefused,
           " fed:", laneFedFrames, ", give up lane");
    bLaneDead = true;
    return DecodeResult::openFailed;
  }
  return DecodeResult::success;
}

void AndVDecoder::updateYuvFormat() {
  if (!mediaCodec) {
    return;
  }
  auto format = AMediaCodec_getOutputFormat(mediaCodec);
  AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_WIDTH, &yuvFormat.width);
  AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_HEIGHT, &yuvFormat.height);
  int32_t localColorFMT = 0;
  AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_COLOR_FORMAT, &localColorFMT);
  AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_STRIDE, &stride);
  // KEY_SLICE_HEIGHT 宏是API 28+符号, 直接用字面量 (值即"slice-height")
  AMediaFormat_getInt32(format, "slice-height", &sliceHeight);
  yuvFormat.type = andYuvType(localColorFMT);
  if (yuvFormat.type == YuvType::other) {
    // c2 flexible回报不出标准枚举, 按请求的420P对待 (QC2 byte-buffer输出YV12布局)
    yuvFormat.type = YuvType::yuv420P;
  }
  // 取证探针: 实际回报的输出格式(与 request 探针对读, 降级在此可见)
  fprintf(stderr,
          "[andvdec] output colorFmt=0x%x -> yuvType=%d %dx%d stride=%d "
          "slice=%d\n",
          localColorFMT, (int)yuvFormat.type, yuvFormat.width,
          yuvFormat.height, stride, sliceHeight);
}

DecodeResult AndVDecoder::decode(const AvoxPacket& packet) {
  if ((codecDesc.vcodecId == VCodecId::h264 && configPackets.size() < 2) ||
      (codecDesc.vcodecId == VCodecId::h265 && configPackets.size() < 3)) {
    return DecodeResult::noConfig;
  }
  if (!bOpen) {
    return onPreDecoder();
  }
  // 车道已判死: 直接让出(上层换道), 别再往 Released 态的 codec 里灌包
  DecodeResult lane = checkLaneAlive();
  if (lane != DecodeResult::success) {
    return lane;
  }
  // 1.0 当前包先入待喂队列再统一入队: input buffer 全占时直接丢包=缺slice
  // 宏块花屏(9/25 真机硬解定案), 未入队的包跨调用补喂, seek复位清空
  pendingPackets.emplace_back(packet.data.data,
                              packet.data.data + packet.data.size);
  pendingPts.push_back(packet.pts);
  while (!pendingPackets.empty()) {
    ssize_t bufidx = AMediaCodec_dequeueInputBuffer(
        mediaCodec, AVOX_ANDROID_MEDIACODEC_TIMEOUT_US);
    if (bufidx < 0) {
      // 输入buffer都占着: 有可能是解码慢跟不上输入速度, 留到下轮补喂,
      // 先走后面的输出排空腾位
      if (!bLaneErrLogged) {
        bLaneErrLogged = true;
        LOGFLF(LogLevel::info, "android video decoder dequeue input ret:",
               (int64_t)bufidx);
      }
      // 返回值要判: 等空位的正常态是 WOULD_BLOCK/-1; 落进媒体错误码区间
      // 说明 codec 已不在可用态(驱动报错后被框架判 Released, 不会自愈)
      if (isDequeHardError(bufidx) && ++laneDequeErrs >= 2) {
        LOGFLF(LogLevel::warn, "android video decoder dequeue input hard err:",
               (int64_t)bufidx, ", give up lane");
        bLaneDead = true;
        return DecodeResult::openFailed;
      }
      ++laneRefused;
      break;
    }
    laneRefused = 0;
    laneDequeErrs = 0;
    size_t bufsize = 0;
    uint8_t* buf = AMediaCodec_getInputBuffer(mediaCodec, bufidx, &bufsize);
    auto& data = pendingPackets.front();
    // 新增缓冲区大小检查
    if (data.size() > bufsize) {
      log(LogLevel::warn,
          "android video decoder input buffer overflow bufsize:", bufsize,
          " packetSize:", data.size());
      AMediaCodec_queueInputBuffer(mediaCodec, bufidx, 0, 0, 0,
                                   AMEDIACODEC_BUFFER_FLAG_PARTIAL_FRAME);
    } else {
      memcpy(buf, data.data(), data.size());
      // 入队列 给到解码器
      media_status_t qs = AMediaCodec_queueInputBuffer(
          mediaCodec, bufidx, 0, data.size(), pendingPts.front() * 1000, 0);
      // 入队失败: 健康车道不会出现(index 刚从本 codec 拿到、尺寸已校验)。
      // 连续两次即判死 —— 驱动不可恢复报错后 MediaCodec 进 Released 不自愈
      if (qs != AMEDIA_OK) {
        if (++laneQueueFails >= 2) {
          LOGFLF(LogLevel::warn, "android video decoder queue failed:", qs,
                 ", give up lane");
          bLaneDead = true;
          return DecodeResult::openFailed;
        }
        break;
      }
      laneQueueFails = 0;
      if (laneFedFrames++ == 0) {
        laneStartMs = timeStampMS();
      }
      if (andPtsLog()) {
        fprintf(stderr, "APTS in=%lld\n", (long long)pendingPts.front());
      }
    }
    pendingPackets.pop_front();
    pendingPts.pop_front();
  }
  // 2 取输出，拿走数据，归还buffer
  size_t bufsize = 0;
  while (mediaCodec) {
    AMediaCodecBufferInfo info = {};
    ssize_t bufidx = AMediaCodec_dequeueOutputBuffer(mediaCodec, &info, 0);
    if (yuvFormat.width == 0 || yuvFormat.height == 0) {
      updateYuvFormat();
    }
    if (bufidx < 0) {
      // 大小变化
      if (bufidx == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
        updateYuvFormat();
      }
      // 数据不够，等下次,重试
      if (bufidx == AMEDIACODEC_INFO_TRY_AGAIN_LATER) {
      }
      if (bufidx == AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED) {
      }
      // 输出侧硬错同样是"codec 不在可用态": 健康车道只给 -1/-2/-3 或正常索引
      if (isDequeHardError(bufidx) && ++laneDequeErrs >= 2) {
        LOGFLF(LogLevel::warn, "android video decoder dequeue output hard err:",
               (int64_t)bufidx, ", give up lane");
        bLaneDead = true;
        return DecodeResult::openFailed;
      }
      return DecodeResult::dataNoReady;
    }
    laneDequeErrs = 0;
    // 交付过输出: 守卫转为"零输出"以外的话题(停滞/拒绝判定不再适用)。
    // 顺手记下首帧成绩(喂帧数/被拒次数/耗时) —— 守卫门限 kLaneRefusedMax 的
    // 安全余量是否够, 靠这条实测(健康车道该远小于门限)
    if (!bLaneOut) {
      LOGFLF(LogLevel::info, "android video decoder first out, fed:",
             laneFedFrames, " refused:", laneRefused, " ms:",
             laneStartMs == 0 ? 0 : timeStampMS() - laneStartMs);
    }
    bLaneOut = true;
    // 输入的buffer已解析完毕，结束当前while循环
    if (bufidx == AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) {
      return DecodeResult::success;
    }
    // 微秒转毫秒
    int64_t rawPtsUs = info.presentationTimeUs / 1000;
    if (andPtsLog()) {
      fprintf(stderr, "APTS out=%lld\n", (long long)rawPtsUs);
    }
    // LOGFLF(LogLevel::info, "pts:", rawPtsUs);
    if (bOpenglRender) {
      //  https://blog.csdn.net/weiwei9363/article/details/135908473
      //  如果 render 参数为true，那么解码后的数据（Buffer）会立即被送到 Surface
      //  进行渲染（播放），播放完后，这个 Buffer 就会被标记为可用，返回给
      //  MediaCodec。如果 render 参数为 false，那么这个 Buffer 不会被送到
      //  Surface，而是直接被释放，然后被标记为可用，返回给 MediaCodec。
      // 这句调用要在队列取出之后
      // AMediaCodec_releaseOutputBuffer(mediaCodec, bufidx, false);
      GpuFrame frame = {};
      frame.pts = rawPtsUs;
      frame.dts = frame.pts;
      frame.context = this;
      frame.format = yuvFormat;
      frame.queueIndex = bufidx;
      if (info.flags == 1) {
        frame.keyFrame = true;
      }
      dispatch(&IVideoDecoderOb::onDecodeGpu, frame);
    } else {
      YUVFrame frame = {};
      frame.format = yuvFormat;
      frame.pts = rawPtsUs;
      frame.dts = frame.pts;
      uint8_t* out = AMediaCodec_getOutputBuffer(mediaCodec, bufidx, &bufsize);
      // byte-buffer输出按KEY_STRIDE/KEY_SLICE_HEIGHT切三平面,
      // 只填data[0]会让下游按null/0平面拷贝直接段错误
      frame.data[0] = out ? out + info.offset : nullptr;
      frame.stride[0] = stride > 0 ? stride : yuvFormat.width;
      int32_t sliceH = sliceHeight > 0 ? sliceHeight : yuvFormat.height;
      if (yuvFormat.type == YuvType::nv12 ||
          yuvFormat.type == YuvType::p010) {
        // 半平面(半平面10bit的p010即NV12的16bit容器形态): Y面 + UV交织单面,
        // 行距同为字节口径(p010 stride=width*2); 落 420P 平面分支会把 UV
        // 拆成两个独立平面 → 布局解析错乱画面烂掉 (2026-09-30 真机实证)
        frame.data[1] = frame.data[0] + (uint64_t)frame.stride[0] * sliceH;
        frame.stride[1] = frame.stride[0];
      } else {
        int32_t uvStride = (frame.stride[0] + 1) / 2;
        int32_t uvSliceH = (sliceH + 1) / 2;
        frame.data[1] = frame.data[0] + (uint64_t)frame.stride[0] * sliceH;
        frame.data[2] =
            frame.data[1] + (uint64_t)uvStride * uvSliceH;
        frame.stride[1] = uvStride;
        frame.stride[2] = uvStride;
      }
      // https://developer.android.com/reference/android/media/MediaCodec.BufferInfo.html
      // 1 keyframe 2 config 4 end of stream
      if (info.flags == 1) {
        frame.keyFrame = true;
      }
      // 加入队列
      dispatch(&IVideoDecoderOb::onDecode, frame);
      // 取证探针(只打一次): dump P010 原始字样, 验证 vendor 真实布局与
      // 假设(Y 行距1280B, UV 起点在 stride*sliceH, 高 10 位对齐)是否一致。
      // 锚点素材期望: y[0]=0x0000(黑条), y[639]=0xffc0(白条), uv=0x8000(512)
      static int32_t sP010Probe = 0;
      if (yuvFormat.type == YuvType::p010 && out && sP010Probe++ < 1 &&
          bufsize > (int32_t)(1280 * 450)) {
        auto* w0 = (const uint16_t*)(out + info.offset);
        auto* ymid = (const uint16_t*)(out + info.offset + 1280 * 180);
        auto* uv = (const uint16_t*)(out + info.offset + (uint64_t)1280 * 360 +
                                     1280 * 90);
        fprintf(stderr,
                "[andvdec] p010 y0=%04x y639=%04x ymid0=%04x ymid639=%04x "
                "uv0=%04x uv1=%04x uv2=%04x\n",
                w0[0], w0[639], ymid[0], ymid[639], uv[0], uv[1], uv[2]);
      }
      // CPU在这直接释放mediaCodec缓冲区中相应的资源
      AMediaCodec_releaseOutputBuffer(mediaCodec, bufidx, false);
    }
  }
  return DecodeResult::success;
}

void AndVDecoder::flush() {
  if (!mediaCodec) {
    return;
  }
  // 原地flush连带清跨调用补喂的旧位置包: 残留会先于落点包喂入,
  // 令codec丢到下个sync frame(有keyframe gate兜底, 但白丢一段)
  pendingPackets.clear();
  pendingPts.clear();
  AMediaCodec_flush(mediaCodec);
  // seek 复位后重开守卫窗口(不碰 bLaneDead): 驱动在复位后翻脸同族
  bLaneOut = false;
  laneQueueFails = 0;
  laneDequeErrs = 0;
  laneRefused = 0;
  laneStartMs = 0;
  laneFedFrames = 0;
}

void AndVDecoder::onClose() {
  bOpen = false;
  pendingPackets.clear();
  pendingPts.clear();
  if (mediaCodec) {
    // AMediaCodec_flush(mediaCodec);
    AMediaCodec_stop(mediaCodec);
    AMediaCodec_delete(mediaCodec);
    mediaCodec = nullptr;
  }
  if (format) {
    AMediaFormat_delete(format);
    format = nullptr;
  }
  if (surfaceTexture) {
    surfaceTexture->close();
    surfaceTexture.reset();
  }
  unInit();
}

// 硬解由对应的EGLWindows窗口Tick线程调用，其窗口相应的GLES上下文共享给MediaCodec
void AndVDecoder::onFrameRelease(bool bRender, const GpuFrame& frame) {
  if (!mediaCodec || frame.queueIndex < 0) {
    return;
  }
  // bRender为true,硬解队列数据压入OES纹理
  media_status_t status =
      AMediaCodec_releaseOutputBuffer(mediaCodec, frame.queueIndex, bRender);
  if (status != AMEDIA_OK && status != -13) {
    LOGFLF(LogLevel::warn, "failed:", status);
    return;
  }
  if (!bOpenglRender) {
    return;
  }
  if (!surfaceTexture) {
    LOGFLF(LogLevel::warn, "surface texture is null");
    return;
  }
  // && bFrameAvailable
  if (bRender) {
    // 取图必须在持有该 OES 纹理的 EGL 上下文线程上; seek 重置会重建渲染图并换上下文,
    // 纹理仍挂旧上下文 → 每帧抛 invalid EGLContext(画面冻住/黑屏)。重挂后重试
    if (!surfaceTexture->updateTexImage()) {
      static int64_t sLastTryMs = 0;
      static int32_t sFailCount = 0;
      int64_t nowMs = timeStampMS();
      if (nowMs - sLastTryMs > 1000) {
        sLastTryMs = nowMs;
        bool bOk = surfaceTexture->detachFromGLContext() &&
                   surfaceTexture->attachToGLContext((int32_t)textureId) &&
                   surfaceTexture->updateTexImage();
        LOGFLF(LogLevel::warn, "updateTexImage failed, reattach:",
               bOk ? "ok" : "failed", " tex:", (int32_t)textureId,
               " n:", ++sFailCount);
      }
    }
    bFrameAvailable = false;
  }
}

#endif

}
