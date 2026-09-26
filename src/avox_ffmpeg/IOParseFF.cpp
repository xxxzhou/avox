#include "IOParseFF.hpp"

#include "PgsDecoder.hpp"

#include <libavutil/intreadwrite.h>
#include <libavutil/log.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <thread>

#include "avox/Avox.hpp"
#include "avox/codec/H26XHelper.hpp"
#include "avox/module/AvoxManager.hpp"
#include "avox/module/LogHelper.hpp"

namespace avox {

// http 段缓存参数: 段 256KB 整段抓取, 每次抓取顺流预读 kHttpSegPrefetchSegs
// 段(seek 后音视频读区常相距数十 MB, 逐段一请求会被每连接建连成本拖死);
// 普通段预算 16MB LRU; 驱逐后 10s 内又被要的段视为交错锚点(音轨扎堆区)
// 升级进 4MB 钉住区
static constexpr int64_t kHttpSegSize = 256 * 1024;
static constexpr int32_t kHttpSegPrefetchSegs = 8;
static constexpr int64_t kHttpSegLruBytes = 16 * 1024 * 1024;
static constexpr int64_t kHttpSegPinBytes = 4 * 1024 * 1024;
static constexpr int64_t kHttpSegAnchorWinMs = 10 * 1000;

// FFmpeg av_log 级别 -> avox LogLevel
static LogLevel ffToAvoxLevel(int ffLevel) {
  switch (ffLevel) {
    case AV_LOG_QUIET:
      return LogLevel::info;
    case AV_LOG_PANIC:
    case AV_LOG_FATAL:
    case AV_LOG_ERROR:
      return LogLevel::error;
    case AV_LOG_WARNING:
      return LogLevel::warn;
    case AV_LOG_INFO:
      return LogLevel::info;
    case AV_LOG_VERBOSE:
    case AV_LOG_DEBUG:
      return LogLevel::debug;
    default:
      return LogLevel::debug;
  }
}

// 同文消息折叠状态: 解码器逐包刷同一警告时(如 mlp "Stream parameters not
// seen")按窗口限频, 防洪泛拖垮生产线程——LogTask 队列满会在调用线程同步
// 排空, 洪泛把解码线程钉在控制台打印速度上
static std::mutex ffLogFloodMtx;
static std::string ffLogFloodMsg;
static LogLevel ffLogFloodLevel = LogLevel::info;
static int64_t ffLogFloodLastMs = 0;
static int64_t ffLogFloodCount = 0;
static constexpr int64_t kFFLogFloodWindowMs = 3000;

// FFmpeg av_log 回调 -> avox logMsg, 跟 ZLMediaKit onZmLog 同模式
static void onFFLog(void* avcl, int ffLevel, const char* fmt, va_list vl) {
  if (ffLevel <= AV_LOG_QUIET) return;
  // 过滤低于当前 av_log_level 的日志 (av_log 默认只输出 <= warning)
  if (ffLevel > av_log_get_level()) return;
  LogLevel avoxLevel = ffToAvoxLevel(ffLevel);
  // 格式化 FFmpeg 日志消息, 带上模块名(h264/libx265/mpegts等)便于定位来源
  char buf[1024];
  vsnprintf(buf, sizeof(buf), fmt, vl);
  // 解码器逐包 "no frame!" (纯参数集包/首个关键帧前等常态) 纯噪音, 屏掉
  size_t len = strlen(buf);
  while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r' || buf[len - 1] == ' ')) --len;
  if (len == 9 && memcmp(buf, "no frame!", 9) == 0) return;
  std::string msg;
  const AVClass* avc = avcl ? *(const AVClass* const*)avcl : nullptr;
  if (avc && avc->item_name) {
    string_format(msg, "[FF][", avc->item_name((void*)avcl), "] ", buf);
  } else {
    string_format(msg, "[FF] ", buf);
  }
  // 同文折叠: 窗口内重复消息只计次, 换文/超窗补一条累计再放行当前条
  {
    std::lock_guard<std::mutex> lk(ffLogFloodMtx);
    const int64_t nowMs = timeStampMS();
    if (msg == ffLogFloodMsg && nowMs - ffLogFloodLastMs < kFFLogFloodWindowMs) {
      ++ffLogFloodCount;
      return;
    }
    if (ffLogFloodCount > 0) {
      string_format(msg, ffLogFloodMsg, " (+", ffLogFloodCount, " suppressed)");
      logMsg(ffLogFloodLevel, msg.c_str());
    }
    ffLogFloodLevel = avoxLevel;
    ffLogFloodLastMs = nowMs;
    ffLogFloodCount = 0;
    ffLogFloodMsg = std::move(msg);
    logMsg(ffLogFloodLevel, ffLogFloodMsg.c_str());
    return;
  }
}

void regFFIO() {
  RegFunc ffIoReg = {"ffmpeg io init", []() {
                       // 桥接 FFmpeg av_log -> avox logMsg
                       av_log_set_callback(onFFLog);
                       av_log_set_level(AV_LOG_WARNING);
                       IoPlanDesc ffDesc = {};
                       ffDesc.name = "ffmpeg format";
                       AvoxManager::Get().ioSources.regInitFunc(
                           IoPlan::ffmpeg, ffDesc,
                           []() -> AVSource* { return new IOParseFF(); });
                     }};
  AvoxManager::Get().initFuncs.push_back(ffIoReg);
}

// 解码中断回调函数
int decode_interrupt_cb(void* ctx) {
  IOParseFF* ioSource = static_cast<IOParseFF*>(ctx);
  if (!ioSource) {
    return 1;
  }
  // seek 窗口内或 close 后打断 IO 线程阻塞的 av_read_frame: seek 让 seekTo 能
  // 独占 fmtCtx, close 让 stopTask 的 join 不干等对端断开
  return ioSource->interruptIo() ? 1 : 0;
}

IOParseFF::IOParseFF() {
  taskName = "ffmpeg io parse";
  // bDisableAudio = true;
}

IOParseFF::~IOParseFF() {
  // 析构不经 close(): 本级先打断再 join, 拖到 ~RunTask 时 fmtCtx 已析构(线程还在 av_read_frame 会 UAF)
  bStopIo = true;
  stopTask();
  // 线程已 join: 先放 fmtCtx 再关 wrapper/底层 http(CUSTOM_IO 下 pb 归本级管)
  closeCustomAvio();
}

bool IOParseFF::parseStream(int32_t streamId, AVCodecParameters* codecpar) {
  // 如果不是视频和音频，直接返回
  if (codecpar->codec_type != AVMEDIA_TYPE_VIDEO &&
      codecpar->codec_type != AVMEDIA_TYPE_AUDIO &&
      codecpar->codec_type != AVMEDIA_TYPE_SUBTITLE) {
    return true;
  }
  AVBSFContext* vbsf = nullptr;
  int32_t ret = 0;
  if (!bDisableVideo && codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
    if (codecpar->codec_id == AV_CODEC_ID_H264 ||
        codecpar->codec_id == AV_CODEC_ID_H265) {
      // 得到SPS/PPS/VPS信息
      parseH26xConfig(streamId, codecpar->extradata, codecpar->extradata_size,
                      codecpar->codec_id);
    } else if (codecpar->extradata_size > 0) {
      // VC-1/WMV3/RV30/RV40等: 无nalu结构, 容器extradata原样作为vconfig
      // 透传(解码器初始化必需, FFVDecoder 侧按原样组装)
      AvoxPacket vpack = {};
      vpack.data.bRef = true;
      vpack.data.data = codecpar->extradata;
      vpack.data.size = codecpar->extradata_size;
      vpack.prefixSize = 0;
      vpack.packtype = (int32_t)PackType::vconfig;
      vpack.index = vIndexMaps[streamId];
      vpack.pts = 0;
      vpack.dts = 0;
      dispatch(&IAVSourceOb::onPacket, vpack);
      updateConfig(vpack);
    }
  }
  // 字幕轨: ASS/SSA 的 extradata(MKV [Script Info]/[V4+ Styles] 剧本头)以
  // sconfig 包旁路下发, MediaPlayer 侧留存, 选轨时喂 libass(计划 §3.2)。
  // index 与 subtitles 包一致走局部轨索引(与选轨号同域)
  if (codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE) {
    if (codecpar->codec_id == AV_CODEC_ID_HDMV_PGS_SUBTITLE) {
      std::lock_guard<std::mutex> pgsLock(pgsMtx);
      if (!pgsDec) {
        // 首个 PGS 轨: 建解码器(与 sIndexMaps 填充时序解耦 —— 该映射在播放器
        // 首查源信息时才填, parseStream 时点等值比较恒假, 曾致 PGS 全链不通)
        pgsDec = std::make_unique<PgsDecoder>();
        if (!pgsDec->open(codecpar)) {
          pgsDec.reset();
          LOGFLF(LogLevel::warn, "pgs decoder open failed");
        } else {
          pgsStreamId = streamId;
          LOGFLF(LogLevel::info, "pgs decoder ready, stream:", streamId);
        }
      }
    }
    if (ffSCodec(codecpar->codec_id) == SCodecId::ass &&
        codecpar->extradata_size > 0 && streamId >= 0 &&
        streamId < (int32_t)sIndexMaps.size()) {
      AvoxPacket spack = {};
      spack.data.bRef = true;
      spack.data.data = codecpar->extradata;
      spack.data.size = codecpar->extradata_size;
      spack.prefixSize = 0;
      spack.packtype = (int32_t)PackType::sconfig;
      spack.index = sIndexMaps[streamId];
      spack.pts = 0;
      spack.dts = 0;
      dispatch(&IAVSourceOb::onPacket, spack);
    }
    return true;
  }
  if (!bDisableAudio && codecpar->codec_type == AVMEDIA_TYPE_AUDIO &&
      skipAudioStreams.count(streamId) == 0 &&
      codecpar->extradata_size > 0) {
    if (codecpar->codec_id == AV_CODEC_ID_AAC) {
      parseAACConfig(streamId, codecpar->extradata, codecpar->extradata_size);
      bAACExtradata = true;
    } else {
      AvoxPacket apack = {};
      apack.data.bRef = true;
      apack.data.data = codecpar->extradata;
      apack.data.size = codecpar->extradata_size;
      apack.prefixSize = 0;
      apack.packtype = (int32_t)PackType::aconfig;
      apack.index = aIndexMaps[streamId];
      apack.pts = 0;
      apack.dts = 0;
      dispatch(&IAVSourceOb::onPacket, apack);
      // 保存音频配置信息
      // updateConfig(apack);
    }
  }
  return true;
}

bool IOParseFF::parseH26xConfig(int32_t streamId, const uint8_t* extradata,
                                int32_t size, AVCodecID codecId) {
  std::vector<PacketBuf> packets;
  AvoxData extradataBuf = {(uint8_t*)extradata, std::min(50, size), true};
  log(LogLevel::info, "ffmpeg io extradata size:", extradataBuf);
  // 直播Annexb,本地avcc 检查是否是AVCC格式
  if (checkAnnexbHeader(extradata, size) > 0) {
    if (codecId == AV_CODEC_ID_H264) {
      h264SplitNalu(extradata, size, packets);
    } else {
      h265SplitNalu(extradata, size, packets);
    }
    bvcc = false;
  } else {
    if (codecId == AV_CODEC_ID_H264) {
      h264SplitAvcc(extradata, size, packets);
    } else {
      h265SplitHvcc(extradata, size, packets);
    }
    bvcc = true;
  }
  for (auto& packet : packets) {
    AvoxPacket sps_pps = {};
    sps_pps.data.bRef = true;
    sps_pps.data.data = packet.buff.data();
    sps_pps.data.size = packet.size;
    sps_pps.prefixSize = packet.prefixSize;
    sps_pps.pts = 0;
    sps_pps.dts = 0;
    sps_pps.packtype = (int32_t)PackType::vconfig;
    sps_pps.index = vIndexMaps[streamId];
    dispatch(&IAVSourceOb::onPacket, sps_pps);
    // 保存SPS/PPS/VPS信息
    updateConfig(sps_pps);
  }
  return true;
}

void IOParseFF::parseAACConfig(int32_t streamId, const uint8_t* extradata,
                               int32_t size) {
  // ASC格式信息
  if (size >= 2) {
    // 前两个字节是音频配置信息
    unsigned int config = (extradata[0] << 8) | extradata[1];
    // 1:AAC-Main 2:AAC-SLC 2:AAC-SSR 3:AAC-LTP
    // 解析AAC对象类型
    int profile = (config >> 11) & 0x1F;
    // 解析采样率索引
    int sampling_frequency_index = (config >> 7) & 0x0F;
    static const int sampling_frequencies[] = {96000, 88200, 64000, 48000,
                                               44100, 32000, 24000, 22050,
                                               16000, 11025, 8000,  7350};
    // 解析声道配置
    int channel_configuration = (config >> 3) & 0x0F;
    LOGFLF(LogLevel::info,
           "aac sample rate:", sampling_frequencies[sampling_frequency_index],
           " profile:", profile, " channel:", channel_configuration);
    // ASC格式信息
    AvoxPacket asc = {};
    asc.data.bRef = true;
    asc.data.data = (uint8_t*)extradata;
    asc.data.size = size;
    asc.prefixSize = 0;
    asc.packtype = (int32_t)PackType::aconfig;
    asc.index = aIndexMaps[streamId];
    asc.pts = 0;
    asc.dts = 0;
    dispatch(&IAVSourceOb::onPacket, asc);
    // 保存音频配置信息
    // updateConfig(asc);
    bAACExtradata = true;
  }
}

bool IOParseFF::parsePgsFrame(int32_t streamId, const AVPacket* pkt,
                              int64_t ptsMs) {
  (void)streamId;
  if (!pgsDec) {
    return false;
  }
  const auto result = pgsDec->feed(pkt->data, pkt->size, ptsMs);
  if (result == PgsDecoder::FeedResult::none) {
    return false;
  }
  // 画布内存归解码器所有, 观察者同步拷贝(onPgsFrame 约定)
  dispatch(&IAVSourceOb::onPgsFrame, pgsDec->canvas());
  return true;
}

// 选轨重定向(AVSource 钩子覆写, 命令线程): 选中另一条 PGS 流时按其 codecpar
// 换解码器 —— 此前解码器钉死首个 PGS 流, 多 PGS 轨的蓝光原盘选第 2 条会错出
// 第 1 条的字幕。非 PGS 轨/未映射不动(视图侧仲裁显示)
void IOParseFF::onSelectedSubtitle(int32_t localIndex) {
  std::lock_guard<std::mutex> pgsLock(pgsMtx);
  if (localIndex < 0 || !fmtCtx ||
      localIndex >= (int32_t)subtitleTracks.size()) {
    return;
  }
  const int32_t streamId = subtitleTracks[localIndex].trackId();
  if (streamId < 0 || streamId == pgsStreamId ||
      streamId >= fmtCtx->nb_streams) {
    return;
  }
  auto* st = fmtCtx->streams[streamId];
  if (st->codecpar->codec_type != AVMEDIA_TYPE_SUBTITLE ||
      st->codecpar->codec_id != AV_CODEC_ID_HDMV_PGS_SUBTITLE) {
    return;
  }
  auto dec = std::make_unique<PgsDecoder>();
  if (!dec->open(st->codecpar)) {
    LOGFLF(LogLevel::warn, "pgs decoder switch open failed, stream:", streamId);
    return;
  }
  pgsDec = std::move(dec);
  pgsStreamId = streamId;
  LOGFLF(LogLevel::info, "pgs decoder switched to stream:", streamId);
}

// 重建探测字典并 avformat_open_input(fmtCtx 接管), 返回 ffmpeg 错误码(0=成功)。
// FFmpeg9 起 find_stream_info 返回即释放各流探测态(sti->info), 同上下文二次调用踩空指针,
// fast probe 保底补查只能整链重开, 故从 onRunTask 抽出复用。
int IOParseFF::reopenInput() {
  AVFormatContext* temp = avformat_alloc_context();
  temp->interrupt_callback.callback = decode_interrupt_cb;
  temp->interrupt_callback.opaque = this;
  AVDictionary* dict = nullptr;
  LOGFLF(LogLevel::info, "ffmpeg io timeout ms:", timeoutMs,
         " rtsp transport:", rtspTransport);
  // timeoutMs 转为微秒 str
  const std::string timeoutStr = std::to_string(timeoutMs * 1000);
  // 最大延迟 以微秒为单位
  av_dict_set(&dict, "max_delay", timeoutStr.c_str(), 0);
  // 连接超时 以微秒为单位
  av_dict_set(&dict, "stimeout", timeoutStr.c_str(), 0);
  // http/tcp 读超时(微秒): OSS 半开连接(对端静默丢弃不发 RST)时, 不加此项
  // av_read_frame 会无限阻塞, IO 线程卡死; 加了之后超时返回错误走重连路径.
  // stimeout 只对 RTSP 生效, http 不认, 故额外加 timeout
  // RTMP 系列必须跳过: rtmpproto 的 "timeout" 语义是"等待入连接的秒数"且 implies
  // listen —— 传微秒值会把拉流变成监听端, 且 timeout*1000 溢出 int
  // (8000ms -> listen_timeout=-589934592), 表现为 avformat_open_input 直接失败
  const bool bRtmpUrl = url.rfind("rtmp", 0) == 0;
  if (bRtmpUrl) {
    // rtmp 改用 avio 层通用读写超时(微秒), 拿到同样的防卡死保护且无 listen 副作用
    av_dict_set(&dict, "rw_timeout", timeoutStr.c_str(), 0);
  } else {
    av_dict_set(&dict, "timeout", timeoutStr.c_str(), 0);
  }
  // RTSP 相关: 使用 TCP 传输(本地文件/HTTP 会忽略)
  av_dict_set(&dict, "rtsp_transport", rtspTransport.c_str(), 0);
  // 重排队列大小
  av_dict_set(&dict, "reorder_queue_size", "2000", 0);
  // 10MB 缓冲区
  av_dict_set(&dict, "buffer_size", "10485760", 0);
  // HTTP/HTTPS 断线自动重连: OSS/CDN 的 keep-alive 被 server 单方面断开时
  // (seek 后命中死连接 -> "partial file" / TLS 握手失败),
  // 自动重建连接而非致命错误. 不开 reconnect_at_eof, 避免真直播流无限重连
  av_dict_set(&dict, "reconnect", "1", 0);
  av_dict_set(&dict, "reconnect_streamed", "1", 0);
  av_dict_set(&dict, "reconnect_delay_max", "5", 0);
  // 关闭 http 连接复用(keep-alive): 半开死连接(对端静默丢弃)若被复用, 重连
  // 后仍走死连接拿不到数据; 关闭后每次读取新建连接, 半开故障自然隔离.
  // fmp4 本就多 range 短连接, 性能影响可忽略
  // 仅音频转录等 seek 密集场景经 io.http.persistent 开 keep-alive:
  // discard 视频后音频采样在文件里被视频数据隔开, 每 seek 一次 range 请求,
  // 短连接下就是一次 TCP+TLS 握手, 公网上开销不可忽略
  av_dict_set(&dict, "http_persistent", httpPersistent ? "1" : "0", 0);
  // 云盘直链等校验 UA/携带鉴权头的源(io.http.useragent / io.http.headers);
  // 头块为 CRLF 分隔的 "Key: value" 行, 仅 http 协议消费, 本地文件无感
  if (!httpUserAgent.empty()) {
    av_dict_set(&dict, "user_agent", httpUserAgent.c_str(), 0);
  }
  if (!httpHeaders.empty()) {
    av_dict_set(&dict, "headers", httpHeaders.c_str(), 0);
  }
  // 强制重复发送 SPS/PPS
  // av_dict_set(&dict, "repeat_headers", "1", 0);
  // http 直链 wrapper+段缓存: 迅雷/Twitch 等逐段拼装的 mp4 两类病灶——①多
  // mdat 结构 mov 打开时逐 leaf box avio_skip, 每 box 一次断连重连(~150ms/次,
  // 实测一片 9117 个 mdat 卡 11 分钟+); ②音频轨锚在文件头/尾与视频读位相距
  // 数十 MB, 按 DTS 交错吐包每包一次跨 MB range 重连(实测每秒内容 ~70 包,
  // 播 60s 只走 42s, playing/buffering 永动, 用户表现为时间来回跳+一直缓冲)。
  // 解法: 全量 http 走 avio_open2 自开通道 + 自定义 wrapper; 预扫命中多 mdat
  // 时在 open_input 窗口内对 AVSEEK_SIZE 谎报文件大小=第一 mdat 末尾, mov 扫
  // 描在第一个 mdat 处早退(FFmpeg9 mov.c:9967, 该分支不设 next_root_atom 毒,
  // 样本索引照建); 读路径经 256KB 段缓存, 回跳落缓存零重连, 顺序前进每段才
  // 一次请求, 且每抓取顺流预读 kHttpSegPrefetchSegs 段(音视频 seek 落点相距
  // 数十 MB 的双区读, 逐段一请求会被建连成本拖死)。open 返回即停谎报, seek
  // 只记账, 底层位置归段抓取管
  const AVInputFormat* probeFmt = nullptr;
  const bool bHttpUrl =
      url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0;
  if (bHttpUrl) {
    // 协议级选项(timeout/reconnect/UA/headers 等)由 avio_open2 消费, 其余
    // 留在 dict 给 avformat_open_input(demuxer 级), 与原生路径各取所需一致
    if (avio_open2(&httpPb, url.c_str(), AVIO_FLAG_READ,
                   &temp->interrupt_callback, &dict) >= 0) {
      const bool bMultiMdat = prescanHttpBoxes();
      // 4MB wrapper 缓冲: 吸收探测期反复回卷与段间小跳; seekable 继承底层
      // 真实能力(不可寻址的直播 http 不能谎报, 否则 demuxer 决策全歪)
      constexpr int32_t kWrapBufSize = 4 * 1024 * 1024;
      wrapPb = getUniquePtr(avio_alloc_context(
          (uint8_t*)av_malloc(kWrapBufSize), kWrapBufSize, 0, this, wrapReadCb,
          nullptr, wrapSeekCb));
      if (wrapPb) {
        wrapPb->seekable = httpPb->seekable;
        temp->pb = wrapPb.get();
        // 预扫直接读了底层通道: wrapper 从文件 0 起映射, 回卷供首读
        avio_seek(httpPb, 0, SEEK_SET);
        // 探测在谎报窗口外(wrapper 可 seek 正常回卷); 失败置空让 open_input
        // 自行再探, 与原生路径同语义
        if (av_probe_input_buffer2(wrapPb.get(), &probeFmt, url.c_str(), temp,
                                   0, 0) < 0) {
          probeFmt = nullptr;
        }
        bLieSize = bMultiMdat;
      } else {
        // wrapper 建不起来: 拆自开通道, 原生 avformat_open_input 自开兜底
        closeCustomAvio();
      }
    }
  }
  int ret = avformat_open_input(&temp, url.c_str(), probeFmt, &dict);
  bLieSize = false;
  av_dict_free(&dict);
  if (ret < 0) {
    AVOX_FFMEPG_LOG(ret, "avformat_open_input failed")
    avformat_free_context(temp);
    closeCustomAvio();
    return ret;
  }
  fmtCtx = getUniquePtr(temp);
  return 0;
}

// http 头部预扫: 顺序读一个 64KB 窗口解析顶层 box(faststart 的 ftyp/moov 头
// 乃至小 moov 后的 mdat 头都在窗口内, 常规文件零 seek 零额外连接); moov 过大
// 时 mdat 头落窗口外, 用一次 seek+16B 读补齐。命中「moov 后跟 mdat 且该 mdat
// 不延伸到文件尾」的多 box 结构(逐段落盘特征)返回 true 并记 mdat1End; 常规
// 单 mdat(含 moov 在尾/无 moov)返回 false 走原生路径
bool IOParseFF::prescanHttpBoxes() {
  mdat1End = 0;
  if (!httpPb) {
    return false;
  }
  constexpr int32_t kWinSize = 64 * 1024;
  std::vector<uint8_t> win(kWinSize);
  const int32_t n = avio_read(httpPb, win.data(), kWinSize);
  if (n < 8) {
    return false;
  }
  int64_t fileSize = -1;
  bool sawMoov = false;
  int64_t off = 0;
  for (int32_t i = 0; i < 16; i++) {
    int64_t size = 0;
    uint32_t type = 0;
    if (off + 8 <= n) {
      size = AV_RB32(win.data() + off);
      type = AV_RL32(win.data() + off + 4);
      if (size == 1) {
        // largesize 跨窗: 罕见, 放弃
        if (off + 16 > n) {
          return false;
        }
        size = (int64_t)AV_RB64(win.data() + off + 8);
      } else if (size == 0) {
        // box 到文件尾
        if (fileSize < 0) {
          fileSize = avio_size(httpPb);
        }
        size = fileSize - off;
      }
    } else {
      // 窗口尽: 仅在已见 moov 时补读下一个 box 头(命中文件多一次重连)
      if (!sawMoov) {
        return false;
      }
      uint8_t hdr[16] = {};
      if (avio_seek(httpPb, off, SEEK_SET) < 0) {
        return false;
      }
      const int32_t m = avio_read(httpPb, hdr, 16);
      if (m < 8) {
        return false;
      }
      size = AV_RB32(hdr);
      type = AV_RL32(hdr + 4);
      if (size == 1) {
        if (m < 16) {
          return false;
        }
        size = (int64_t)AV_RB64(hdr + 8);
      }
    }
    if (size < 8) {
      return false;  // 坏 box
    }
    const int64_t nextOff = off + size;
    if (type == AV_RL32("moov")) {
      sawMoov = true;
    } else if (type == AV_RL32("mdat") && sawMoov) {
      if (fileSize < 0) {
        fileSize = avio_size(httpPb);
      }
      if (fileSize > 0 && nextOff < fileSize) {
        // mdat 后还有 box: 多 mdat 结构, 命中
        mdat1End = nextOff;
        return true;
      }
      // 单 mdat 到文件尾: 常规文件
      return false;
    }
    off = nextOff;
  }
  return false;
}

void IOParseFF::closeCustomAvio() {
  // 先放 fmtCtx: CUSTOM_IO 下 avformat_close_input 不管 pb, 生命周期归本级
  fmtCtx.reset();
  // 连带 av_free 当前缓冲(libavformat 可能已 realloc 替换过)
  wrapPb.reset();
  // 段缓存连带清空(底层通道随闭失效, 缓存无存在意义)
  segIdxLru.clear();
  segIdxPin.clear();
  segEvictMs.clear();
  segLru.clear();
  segPin.clear();
  segLruBytes = 0;
  segPinBytes = 0;
  if (httpPb) {
    // avio_open2 打开的必须 avio_close(连 URLContext 一起释放)
    avio_closep(&httpPb);
  }
  bLieSize = false;
  mdat1End = 0;
  wrapPos = 0;
}

int IOParseFF::wrapReadCb(void* opaque, uint8_t* buf, int size) {
  auto* self = static_cast<IOParseFF*>(opaque);
  if (!self || !self->httpPb) {
    return AVERROR(EIO);
  }
  const int n = self->wrapReadAt(buf, size, self->wrapPos);
  if (n > 0) {
    self->wrapPos += n;
  }
  return n;
}

int64_t IOParseFF::wrapSeekCb(void* opaque, int64_t offset, int whence) {
  auto* self = static_cast<IOParseFF*>(opaque);
  if (!self || !self->httpPb) {
    return AVERROR(EIO);
  }
  // 谎报窗口(avformat_open_input 期间): AVSEEK_SIZE 报第一 mdat 末尾, mov
  // 顶层扫描即刻命中早退
  if (self->bLieSize && (whence & AVSEEK_SIZE)) {
    return self->mdat1End;
  }
  // 其余 AVSEEK_SIZE 问底层真实大小; SEEK_END 也须先有大小才能折算
  if ((whence & AVSEEK_SIZE) || (whence & ~AVSEEK_FORCE) == SEEK_END) {
    const int64_t sz = avio_seek(self->httpPb, 0, AVSEEK_SIZE);
    if (sz < 0) {
      return sz;
    }
    if (whence & AVSEEK_SIZE) {
      return sz;
    }
    offset += sz;
  } else if ((whence & ~AVSEEK_FORCE) == SEEK_CUR) {
    offset += self->wrapPos;
  } else if ((whence & ~AVSEEK_FORCE) != SEEK_SET) {
    return AVERROR(EINVAL);
  }
  // seek 只记账(数据面全在段缓存, 命中零底层调用; 未命中由抓取搬底层)
  if (offset < 0) {
    return AVERROR(EINVAL);
  }
  // 逻辑 seek 是打断/读错残留的清零点: demuxer 寻位后的解析应从干净状态开始
  // (matroska 解析失败时直接读 pb->error 定死因, 陈年旧错会把合法 seek 毒死;
  // 且 avio_seek 小距离分支只挪指针不清 latch, 必须在此显式清)
  if (self->wrapPb) {
    self->wrapPb->error = 0;
    self->wrapPb->eof_reached = 0;
  }
  self->wrapPos = offset;
  return offset;
}

int IOParseFF::wrapReadAt(uint8_t* buf, int size, int64_t pos) {
  const int64_t segStart = pos / kHttpSegSize * kHttpSegSize;
  HttpSeg* seg = touchHttpSeg(segStart);
  if (!seg) {
    return AVERROR(EIO);
  }
  int64_t inSeg = pos - seg->off;
  int64_t avail = (int64_t)seg->data.size() - inSeg;
  if (avail <= 0) {
    // 短段只在真文件尾是合法事实。中文件的短段(预读期连接被服务端提前收尾
    // 等原因入库)是毒段: mov 在 avio_seek 的前向 fill 循环吃到这个 EOF 会
    // should_retry 撤样本重试, 同段永远 EOF, 刷 partial file 风暴把音频丢光。
    // 按底层文件大小判别, 毒段驱逐重抓一次; 刚治过的段短时间内再毒直接认尾,
    // 防服务端持续截断时的无限重抓
    const int64_t sz = avio_size(httpPb);
    if (sz > 0 && seg->off + (int64_t)seg->data.size() >= sz) {
      return AVERROR_EOF;
    }
    const int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::steady_clock::now().time_since_epoch())
                              .count();
    auto evict = segEvictMs.find(segStart);
    if (evict != segEvictMs.end() && nowMs - evict->second < 3000) {
      return AVERROR_EOF;
    }
    auto& lst = segIdxLru.count(segStart) ? segLru : segPin;
    auto& idx = segIdxLru.count(segStart) ? segIdxLru : segIdxPin;
    auto& bytes = segIdxLru.count(segStart) ? segLruBytes : segPinBytes;
    auto it = idx.find(segStart);
    if (it != idx.end()) {
      bytes -= (int64_t)it->second->data.size();
      lst.erase(it->second);
      idx.erase(it);
    }
    segEvictMs[segStart] = nowMs;
    seg = touchHttpSeg(segStart);
    if (!seg) {
      return AVERROR(EIO);
    }
    inSeg = pos - seg->off;
    avail = (int64_t)seg->data.size() - inSeg;
    if (avail <= 0) {
      return AVERROR_EOF;
    }
  }
  const int n = (int)std::min<int64_t>(size, avail);
  memcpy(buf, seg->data.data() + inSeg, n);
  return n;
}

IOParseFF::HttpSeg* IOParseFF::touchHttpSeg(int64_t segStart) {
  auto hit = segIdxPin.find(segStart);
  if (hit != segIdxPin.end()) {
    return &*hit->second;
  }
  hit = segIdxLru.find(segStart);
  if (hit != segIdxLru.end()) {
    segLru.splice(segLru.begin(), segLru, hit->second);
    segIdxLru[segStart] = segLru.begin();
    return &segLru.front();
  }
  // 驱逐后短期又被要: 该段是交错锚点(音轨扎堆区), 钉住不再驱逐
  const int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now().time_since_epoch())
                            .count();
  auto evict = segEvictMs.find(segStart);
  const bool bAnchor =
      evict != segEvictMs.end() && nowMs - evict->second < kHttpSegAnchorWinMs;
  return fetchHttpSeg(segStart, bAnchor);
}

IOParseFF::HttpSeg* IOParseFF::fetchHttpSeg(int64_t segStart, bool bAnchor) {
  // 服务端瞬断单次即败会退化成 mov partial 风暴: demuxer 对音频样本逐个回退
  // 重试, 音频供给断流 A/V 撕裂(极空间 seek 后实测), 错位包还会喂出垃圾帧。
  // 底层抓取自带短间隔重试吸收瞬断; 重试都失败才交回上层(上层自有无穷重试)。
  // 打断窗口(seek 暂停/close)内 avio_seek 恒吐 AVERROR_EXIT, 重试纯空转,
  // 立即放弃——恢复读循环后按新读位重新取段。
  // 预算 10 次: 每次抓取 avio_seek 新建连接(旧连接被服务端收尾后 seek 即重连),
  // 恶劣网关按连接随机掐杀(极空间 0926 夜实测单连被掐率~25%)时, 耗尽 3 次
  // 的概率~1.6% → 截断包漏给 demuxer 拼成 Invalid NAL → 周期跳帧; 拉到 10 次
  // 后连杀概率降到百万分之一量级
  for (int32_t attempt = 0; attempt < 10; ++attempt) {
    HttpSeg* seg = fetchHttpSegOnce(segStart, bAnchor);
    if (seg) {
      return seg;
    }
    if (interruptIo()) {
      return nullptr;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  return nullptr;
}

// 重建 httpPb 自开通道: 服务端收尾连接是协议层"干净 EOF", http.c 把 eof 闩死,
// 之后 avio_seek 恒吐 AVERROR_EOF(misland), 死通道上重试纯空转(misland 提前
// return 又发生在清闩点之前) —— 只能换新连接。仅协议级选项子集, 与 open 期一致
bool IOParseFF::rebuildHttpPb() {
  if (httpPb) {
    avio_closep(&httpPb);
  }
  AVDictionary* dict = nullptr;
  const std::string timeoutStr = std::to_string(timeoutMs * 1000);
  av_dict_set(&dict, "timeout", timeoutStr.c_str(), 0);
  av_dict_set(&dict, "buffer_size", "10485760", 0);
  av_dict_set(&dict, "reconnect", "1", 0);
  av_dict_set(&dict, "reconnect_streamed", "1", 0);
  av_dict_set(&dict, "reconnect_delay_max", "5", 0);
  av_dict_set(&dict, "http_persistent", httpPersistent ? "1" : "0", 0);
  AVIOInterruptCB cb = {decode_interrupt_cb, this};
  const int ret = avio_open2(&httpPb, url.c_str(), AVIO_FLAG_READ, &cb, &dict);
  av_dict_free(&dict);
  if (ret < 0) {
    AVOX_FFMEPG_LOG(ret, "rebuild httpPb failed")
    return false;
  }
  LOGFLF(LogLevel::info, "httpPb rebuilt after channel eof latch");
  return true;
}

IOParseFF::HttpSeg* IOParseFF::fetchHttpSegOnce(int64_t segStart,
                                                bool bAnchor) {
  // 落点必须逐字节相等: 只查 <0 会放过「返回成功但底层实际落位偏移」的
  // http 瞬断半程(连接重建后 Range 未生效等), 错位数据入库后整段所有包
  // 字节平移——视频包 NAL 边界全错解码不出, 音频靠 AAC 重同步静默吞掉
  // (极空间重访区实测), 表现即 seek 后视频永久断供
  const int64_t landed = avio_seek(httpPb, segStart, SEEK_SET);
  if (landed != segStart) {
    // 打断窗口内恒吐 EXIT 属预期(seek/close 接管), 不刷屏; 其余落位失败留痕
    if (landed != AVERROR_EXIT || !interruptIo()) {
      LOGFLF(LogLevel::warn, "seg fetch seek misland, want:", segStart,
             " got:", landed);
    }
    // 非 EXIT 的 misland(EOF/EIO)=通道被服务端收尾后 http.c eof 闩死: 同通道
    // 重试恒败, 下面俩清闩也救不了 seek(fail 点在 fill_buffer) —— 重建通道,
    // 重试预算里下一轮 attempt 用新连接; 不重建则整份预算在死通道上空转,
    // 段断供→mov 拼截断包(Invalid NAL)→周期跳画(极空间 0926 实证)
    if (landed != AVERROR_EXIT) {
      rebuildHttpPb();
    }
    return nullptr;
  }
  // seek 落位即清底层 avio 的读错/EOF 残留: 打断窗口刚结束的首个抓取会被
  // 上一次的 latch 毒死(fill_buffer 只看 eof_reached, avio_seek 只清它不清
  // error), mkv 经 Cues 在文件尾的合法 seek 就这么被打断残留误杀过
  httpPb->eof_reached = 0;
  httpPb->error = 0;
  HttpSeg seg;
  seg.off = segStart;
  seg.data.resize(kHttpSegSize);
  // avio_read 短读常见(socket 边界), 循环凑满; 仅 EOF 允许入库短段(文件尾),
  // 错误/打断的半截段不入缓存(否则段耗尽误判文件尾提前 EOF)
  int64_t total = 0;
  while (total < kHttpSegSize) {
    const int n =
        avio_read(httpPb, seg.data.data() + total, (int)(kHttpSegSize - total));
    if (n == AVERROR_EOF) {
      break;
    }
    if (n <= 0) {
      // 已读若干字节后吃错: 仅当正好读满文件尾(段起+已读==底层文件大小)才
      // 入库短段——尾部数据是完整事实, 打断/连接收尾杂音不构成丢弃理由(mkv
      // seek 解析尾部 Cues 必经此路, 丢段=seek 判死掉进分钟级内部扫描);
      // 中途吃错照旧丢弃(半截段入库会让后续读到假 EOF)
      if (total > 0) {
        const int64_t sz = avio_size(httpPb);
        if (sz > 0 && segStart + total >= sz) {
          break;
        }
      }
      return nullptr;
    }
    total += n;
  }
  if (total <= 0) {
    return nullptr;
  }
  seg.data.resize((size_t)total);
  // 段追踪(临时诊断口): AVOX_SEG_TRACE=1 时打印每段落位/字节数/首 8 字节,
  // 供与 curl 直读字节对照定位错位层
  char headHex[17] = {0};
  const bool bTrace = [] {
    static const bool b = getenv("AVOX_SEG_TRACE") &&
                          getenv("AVOX_SEG_TRACE")[0] == '1';
    return b;
  }();
  if (bTrace) {
    for (int i = 0; i < 8 && i < (int)seg.data.size(); ++i) {
      snprintf(headHex + i * 2, sizeof(headHex) - i * 2, "%02x",
               seg.data[(size_t)i]);
    }
  }
  auto& lst = bAnchor ? segPin : segLru;
  auto& idx = bAnchor ? segIdxPin : segIdxLru;
  auto& bytes = bAnchor ? segPinBytes : segLruBytes;
  lst.push_front(std::move(seg));
  idx[segStart] = lst.begin();
  bytes += total;
  if (bTrace) {
    LOGFLF(LogLevel::info, "seg fetched off:", segStart, " bytes:", total,
           " head:", headHex, " anchor:", bAnchor ? 1 : 0);
  }
  // 超预算从链表尾驱逐(钉住区至少保留刚入库的这段)
  while (bytes > (bAnchor ? kHttpSegPinBytes : kHttpSegLruBytes) &&
         lst.size() > (size_t)(bAnchor ? 1 : 0)) {
    evictHttpSeg(bAnchor);
  }
  // 顺流预读: 基段抓满才继续(短段=文件尾); 预读段恒进 LRU, 只在 EOF 短段
  // (真文件尾)入库短段, 中途吃错的半截段一律丢弃即停——半截段入库会让后续
  // 读到假 EOF(基段同规则)
  for (int32_t k = 1; k < kHttpSegPrefetchSegs && total >= kHttpSegSize; ++k) {
    const int64_t next = segStart + (int64_t)k * kHttpSegSize;
    HttpSeg pre;
    pre.off = next;
    pre.data.resize(kHttpSegSize);
    int64_t got = 0;
    bool bEof = false;
    bool bErr = false;
    while (got < kHttpSegSize) {
      const int n =
          avio_read(httpPb, pre.data.data() + got, (int)(kHttpSegSize - got));
      if (n == AVERROR_EOF) {
        bEof = true;
        break;
      }
      if (n <= 0) {
        bErr = true;
        break;
      }
      got += n;
    }
    if (got <= 0 || (bErr && got < kHttpSegSize)) {
      break;
    }
    // EOF 短段只在真文件尾入库; 中文件的干净收尾短段是毒段(之后这段永远
    // EOF, 见 wrapReadAt 毒段自愈注释), 丢弃
    if (got < kHttpSegSize) {
      const int64_t sz = avio_size(httpPb);
      if (!(bEof && sz > 0 && next + got >= sz)) {
        break;
      }
    }
    pre.data.resize((size_t)got);
    segLru.push_front(std::move(pre));
    segIdxLru[next] = segLru.begin();
    segLruBytes += got;
    while (segLruBytes > kHttpSegLruBytes && segLru.size() > 0) {
      evictHttpSeg(false);
    }
    if (bEof) {
      break;
    }
  }
  // 防呆: 驱逐台账只增不清长流场景占内存, 超限整表重置(锚点记忆自愈)
  if (segEvictMs.size() > 4096) {
    segEvictMs.clear();
  }
  // 预读驱逐可能挤掉基段(预算临界时), 按 offset 收尾查找, 挤掉即视为失败
  const auto hit = (bAnchor ? segIdxPin : segIdxLru).find(segStart);
  return hit != (bAnchor ? segIdxPin : segIdxLru).end() ? &*hit->second
                                                        : nullptr;
}

void IOParseFF::evictHttpSeg(bool bPin) {
  auto& lst = bPin ? segPin : segLru;
  auto& idx = bPin ? segIdxPin : segIdxLru;
  auto& bytes = bPin ? segPinBytes : segLruBytes;
  if (lst.empty()) {
    return;
  }
  const int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now().time_since_epoch())
                            .count();
  segEvictMs[lst.back().off] = nowMs;
  bytes -= (int64_t)lst.back().data.size();
  idx.erase(lst.back().off);
  lst.pop_back();
}

void IOParseFF::onRunTask() {
  // 起播耗时拆解 (a02-T1): open_input 与 find_stream_info 分段
  const auto ioOpenStart = std::chrono::steady_clock::now();
  int ret = reopenInput();
  if (ret < 0) {
    dispatch(&IAVSourceOb::onError, ffIoError(ret), "open input failed");
    return;
  }
  const int64_t openInputMs =
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - ioOpenStart)
          .count();
  // 检查是否有全局头,相应的SPS/VPS/ATDS保存在extradata里
  if (fmtCtx->iformat->flags & AVFMT_GLOBALHEADER) {
  }
  // probe 降档分档 (a02-T2): 本地索引容器(mp4/mov/mkv/webm/avi/flv)头里就带
  // 编码参数, 缩减探测参数省起播耗时; 网络流/ts/HLS(无索引)保持默认。
  // 保底硬要求(A-12 10bit 预判/A-3 解码选型依赖 codecpar): 降档后宽高/像素
  // 格式/fps/采样率任一缺失, 恢复默认参数补查一次, 不让快路径掏空字段
  const bool bLocalUrl =
      url.find("://") == std::string::npos || url.rfind("file:", 0) == 0;
  const char* fmtName = fmtCtx->iformat ? fmtCtx->iformat->name : "";
  const bool bIndexedContainer =
      strstr(fmtName, "mp4") || strstr(fmtName, "mov,") ||
      strstr(fmtName, "matroska") || strstr(fmtName, "avi") ||
      strstr(fmtName, "flv");
  const bool bFastProbe = bLocalUrl && bIndexedContainer;
  if (bFastProbe) {
    fmtCtx->probesize = 1024 * 1024;          // 1MB
    fmtCtx->max_analyze_duration = 1000000;   // 1s(微秒)
  }
  const auto findInfoStart = std::chrono::steady_clock::now();
  if ((ret = avformat_find_stream_info(fmtCtx.get(), nullptr)) < 0) {
    AVOX_FFMEPG_LOG(ret, "avformat_find_stream_info failed");
    dispatch(&IAVSourceOb::onError, ffIoError(ret), "open stream failed");
    return;
  }
  // 保底字段回退补查: 任一流关键选型字段缺失则按默认参数再探一次
  if (bFastProbe) {
    bool bNeedFullProbe = false;
    for (int32_t i = 0; i < fmtCtx->nb_streams && !bNeedFullProbe; i++) {
      const auto* st = fmtCtx->streams[i];
      if (st->disposition & AV_DISPOSITION_ATTACHED_PIC) {
        continue;
      }
      if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
        bNeedFullProbe = st->codecpar->width <= 0 || st->codecpar->height <= 0 ||
                         st->codecpar->format == AV_PIX_FMT_NONE ||
                         ffFps(st) <= 0;
      } else if (st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
        bNeedFullProbe = st->codecpar->sample_rate <= 0 ||
                         st->codecpar->ch_layout.nb_channels <= 0;
      }
    }
    if (bNeedFullProbe) {
      LOGFLF(LogLevel::info,
             "[metrics] fast probe insufficient, fallback full probe");
      // FFmpeg9 探测态已随上次 find_stream_info 释放: 重开上下文按默认参数整探
      fmtCtx.reset();
      if ((ret = reopenInput()) < 0) {
        dispatch(&IAVSourceOb::onError, ffIoError(ret), "open input failed");
        return;
      }
      if ((ret = avformat_find_stream_info(fmtCtx.get(), nullptr)) < 0) {
        AVOX_FFMEPG_LOG(ret, "avformat_find_stream_info fallback failed");
        dispatch(&IAVSourceOb::onError, ffIoError(ret), "open stream failed");
        return;
      }
    }
  }
  LOGFLF(LogLevel::info, "[metrics] io open_input_ms:", openInputMs,
         " find_stream_info_ms:",
         std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now() - findInfoStart)
             .count());
  skipAudioStreams.clear();  // 实例可能复用重开, 清掉上次的跳过流记录
  for (int32_t i = 0; i < fmtCtx->nb_streams; i++) {
    auto& st = fmtCtx->streams[i];
    // 跳过封面图等附加静态图流: 不是可播放的视频轨, 且其包会被误判污染
    // avcc/annexb 判定
    if (st->disposition & AV_DISPOSITION_ATTACHED_PIC) {
      continue;
    }
    if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && !bDisableVideo) {
      VTrackDesc vdesc = {};
      vdesc.codecId = ffVCodec(st->codecpar->codec_id);
      vdesc.trackId = st->index;
      // vdesc.timeBase = {st->time_base.num, st->time_base.den};
      vdesc.desc.width = st->codecpar->width;
      vdesc.desc.height = st->codecpar->height;
      vdesc.desc.fps = ffFps(st);
      vdesc.desc.type = ffYuvType((AVPixelFormat)st->codecpar->format);
      vdesc.desc.colorSpace = ffColorSpace(st->codecpar);
      addVideoDesc(vdesc);
    } else if (st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO &&
               !bDisableAudio) {
      ATrackDesc adesc = {};
      adesc.codecId = ffACodec(st->codecpar->codec_id);
      // 不支持的音频格式只跳过该流并 discard 字节, 其余音轨照常(a08):
      // 原行为 bDisableAudio=true 会因一条未知轨株连关闭全部音频
      if (adesc.codecId == ACodecId::none) {
        LOGFLF(LogLevel::warn,
               "unsupported audio codec, skip stream:", st->codecpar->codec_id);
        skipAudioStreams.insert(st->index);
        st->discard = AVDISCARD_ALL;
        continue;
      }
      adesc.trackId = st->index;
      // adesc.timeBase = {st->time_base.num, st->time_base.den};
      adesc.desc.sampleRate = st->codecpar->sample_rate;
      adesc.desc.format = ffAudioFromat(st->codecpar->format);
      adesc.desc.channels = st->codecpar->ch_layout.nb_channels;
      adesc.desc.blockAlign = st->codecpar->block_align;
      addAudioDesc(adesc);
      // 单独给AAC配置头文件使用
      audioDesc = adesc.desc;
    } else if (st->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE) {
      // 字幕流入轨枚举(计划 §3.2): 只登记 ASS/SSA/SRT/MOV_TEXT/PGS, 其余跳过。
      // 数据包经 PackType::subtitles 旁路下发, 不进音视频同步时钟。
      const SCodecId subCodec = ffSCodec(st->codecpar->codec_id);
      if (subCodec == SCodecId::none) {
        LOGFLF(LogLevel::info, "unsupported subtitle codec:",
               st->codecpar->codec_id);
        continue;
      }
      if (subCodec == SCodecId::pgs && !pgsDec) {
        // 解码器在 parseStream 按 codecpar 建, 这里只记流索引用于喂包门控
        pgsStreamId = st->index;
      }
      std::string lang;
      std::string title;
      if (auto* e = av_dict_get(st->metadata, "language", nullptr, 0)) {
        lang = e->value ? e->value : "";
      }
      if (auto* e = av_dict_get(st->metadata, "title", nullptr, 0)) {
        title = e->value ? e->value : "";
      }
      addSubtitleDesc(st->index, subCodec, lang, title,
                      (st->disposition & AV_DISPOSITION_FORCED) != 0);
    }
  }
  // 禁用的流打 AVDISCARD_ALL: 带采样索引的 demuxer(mov/mp4) 对 discard 流不再
  // seek/读字节, 配合可 seek 的 IO(HTTP Range/SMB/本地) 后网络量从整文件降到
  // 所选轨——仅音频转录场景视频字节根本不过网; 顺序读型 demuxer(matroska/mpegts)
  // 只是跳过投递, 不省流量但也无害。必须在 trackReady() 之前(它会把空轨补置禁用)。
  if (bDisableVideo || bDisableAudio) {
    for (int32_t i = 0; i < fmtCtx->nb_streams; i++) {
      auto& st = fmtCtx->streams[i];
      // 封面图流即使不禁用视频也一并 discard: 不是可播放轨, 字节没必要过网
      if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO &&
          (bDisableVideo || (st->disposition & AV_DISPOSITION_ATTACHED_PIC))) {
        st->discard = AVDISCARD_ALL;
      } else if (st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO &&
                 bDisableAudio) {
        st->discard = AVDISCARD_ALL;
      }
    }
  }
  SeekType stype = seekType();
  bSeek = false;
  if (stype != SeekType::none) {
    bSeek = true;
  }
  if (checkLocalPath(url.c_str())) {
    // 本地文件(非流媒体协议),单独区分以便按本地策略处理
    sourceMode = AVSourceMode::local;
  } else if (fmtCtx->duration <= 0) {
    sourceMode = AVSourceMode::live;
  } else {
    sourceMode = AVSourceMode::downLive;
  }
  trackReady();
  bAACExtradata = false;
  // open前可能已设置bFastRead+speed>1，初始化完成后应用非阻塞读取
  if (bFastRead && speed > 1.0) {
    fmtCtx->flags |= AVFMT_FLAG_NONBLOCK;
    LOGFLF(LogLevel::info, "speed:", speed, " set nonblock read on start");
  }
  LOGFLF(LogLevel::info, "io duration:", fmtCtx->duration);
  // 保存解码配置信息
  for (int32_t i = 0; i < fmtCtx->nb_streams; i++) {
    auto& st = fmtCtx->streams[i];
    // 查找流信息，包含SPS/PPS/ATDS等
    if (!parseStream(i, st->codecpar)) {
    }
  }
  // 已经解析配置信息，如SPS,PPS后,开始读取IO数据
  AVPacketPtr bsfPkt = getUniquePtr(av_packet_alloc());
  AVPacketPtr adtsPkt = getUniquePtr(av_packet_alloc());
  bool bEof = false;
  while (running()) {
    // 检查暂停
    if (pauseing()) {
      // 已退出 av_read_frame, 通知 seekTo 可以安全操作 fmtCtx
      bIoPausedAck.store(true);
      sleepTask(false, 10);
      continue;
    }
    if (bEof) {
      // EOF 停放: 不 break(拆线程)。尾包可能还有几十秒数据没被消费, 且之后很可能
      // seek —— 线程一退, seek 只挪了 demuxer 位置却无人再读, 管道从此静止。
      // 这里等 seekTo 复位(bEofReset)或 running() 变假(close), 不再调 av_read_frame
      if (bEofReset.exchange(false)) {
        bEof = false;
        continue;
      }
      sleepTask(false, 10);
      continue;
    }
    AVPacketPtr pkt = nullptr;
    if (!seekStash.empty()) {
      // seekTo 落点校验预读的包优先原序下发(见 seekStash): 校验直读 fmtCtx
      // 的产出不能丢, 尾部关键视频包保证解码从 I 帧起步
      pkt = getUniquePtr(seekStash.front());
      seekStash.pop_front();
    } else {
      // wrapper 模式读前不信任 pb->error: 打断窗口内在飞取段的 EIO/EXIT 会
      // 闩进去, 而小距离 seek 落在 wrapPb 缓冲内时 wrapSeekCb 不被调用、清
      // 不到, 恢复后 fill_buffer 见闩即毙, 首读 2ms 内 onError(极空间拖动
      // 风暴实测, 全程五分钟只撞一次的罕见组合)。自定义读路径每轮都给出
      // 新鲜错误, 闩值一律作废; 本地文件的原生 avio 语义不动
      if (fmtCtx->pb && fmtCtx->pb == wrapPb.get() && !interruptIo()) {
        fmtCtx->pb->error = 0;
      }
      pkt = getUniquePtr(av_packet_alloc());
      if ((ret = av_read_frame(fmtCtx.get(), pkt.get())) < 0) {
        if (ret == AVERROR_EOF) {
          bEof = true;
          if (!bEofNotified.exchange(true)) {
            dispatch(&IAVSourceOb::onComplete);
          }
          continue;
        }
        // 被 seek 打断(interrupt_callback 返回1)或非阻塞暂无数据: 继续循环,
        // 循环顶会处理暂停. 打断窗口(preSeek→pauseTask 之间约 50ms)内循环顶尚
        // 未暂停, 会在此 EXIT→continue 裸转, 必须阻塞 sleep 避免 100% CPU 空转
        if (ret == AVERROR_EXIT || ret == AVERROR(EAGAIN)) {
          // seek 打断窗口的首个 EXIT 会被 avio 闩进 pb->error: 窗口结束后
          // 回调已不打断(intr 清零), av_read_frame 仍恒吐闩值, 读线程从此
          // 空转, EOF 永远到不了 -> onComplete 不发, 播放器在片尾 buffering
          // 到看门狗超时(短视频 seek 尾部必撞). 判为闩残留, 清掉重读
          if (ret == AVERROR_EXIT && !interruptIo() && fmtCtx &&
              fmtCtx->pb && fmtCtx->pb->error == AVERROR_EXIT) {
            fmtCtx->pb->error = 0;
          }
          sleepTask(false, 1);
          continue;
        }
        // seek 打断窗口内(preSeek/seekTo 置位 bInterruptRead)的 INVALIDDATA
        // 是打断的副产物: 网络读被截停时 mov demuxer 把它报成 "partial
        // file"(而非干净的 EXIT), 不是真实故障. 直接 continue 回循环顶, 等
        // seekTo pauseTask→pauseing() 持 ack 安全重定位
        if (bInterruptRead.load()) {
          continue;
        }
        // 其余 IO 错误(OSS 截断 range 致 INVALIDDATA / 连接断开等): 上报错误。
        // close 在拆(bStopIo)时的 EIO 是打断在飞读取的副产物, 不上报——
        // 否则每次正常关闭都给 app 发一条假错误污染日志与归因
        AVOX_FFMEPG_LOG(ret, "read frame failed");
        if (!bStopIo.load()) {
          dispatch(&IAVSourceOb::onError, ffIoError(ret), "read frame failed");
        }
        break;
      }
    }
    int32_t streamId = pkt->stream_index;
    auto st = fmtCtx->streams[streamId];
    // 跳过封面图等附加静态图流的数据包, 避免污染 avcc/annexb 判定(见 onRunTask
    // 建流处)
    if (st->disposition & AV_DISPOSITION_ATTACHED_PIC) {
      continue;
    }
    PackType packType = PackType::other;
    if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
      packType = PackType::video;
      if (bDisableVideo) {
        continue;
      }
      // seek 后等关键帧(见 bWaitKeyframe 成员注释): 非关键包不喂解码器
      if (bWaitKeyframe.load()) {
        if (pkt->flags & AV_PKT_FLAG_KEY) {
          bWaitKeyframe.store(false);
          LOGFLF(LogLevel::info, "seek keyframe gate passed, dropped:",
                 waitKeyframeDrops);
        } else if (++waitKeyframeDrops > 500) {
          bWaitKeyframe.store(false);
          LOGFLF(LogLevel::warn, "seek keyframe gate: no keyframe in ",
                 waitKeyframeDrops, " packets, passing through");
        } else {
          continue;
        }
      }
    } else if (st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
      packType = PackType::audio;
      // 跳过的不支持音频流不进路由: aIndexMaps 无映射, 会错轨/越界
      if (bDisableAudio || skipAudioStreams.count(streamId) > 0) {
        continue;
      }
    } else if (st->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE) {
      // PGS: 选中轨的包进解码器出画布, 不再作为原始包旁路下发
      if (pkt->pts != AV_NOPTS_VALUE) {
        // 锁内判定+喂包: pgsDec/pgsStreamId 可被选轨线程并发重定向
        std::lock_guard<std::mutex> pgsLock(pgsMtx);
        if (pgsDec && streamId == pgsStreamId) {
          const int64_t pgsPts =
              av_rescale_q(pkt->pts, st->time_base, {1, 1000});
          parsePgsFrame(streamId, pkt.get(), pgsPts);
          continue;
        }
      }
      packType = PackType::subtitles;
      // 旁路数据量极小(每条对白几十字节), 不按 a/v 的禁用开关丢弃;
      // 无消费者时 MediaPlayer 侧自然忽略
      if (pkt->size <= 0) {
        continue;
      }
    }
    // 忽略其他类型
    if (packType == PackType::other) {
      continue;
    }
    int32_t prefixSize = 0;
    AVPacket* refPkt = pkt.get();
    if (packType == PackType::video) {
      prefixSize = 4;
    } else if (packType == PackType::audio) {
      if (st->codecpar->codec_id == AV_CODEC_ID_AAC) {
        // 添加adts头，前面可能没有extradata数据,在这添加
        if (bAdtsHeader((uint8_t*)pkt->data, pkt->size) && !bAACExtradata) {
          // 如果是AAC流,并且没有extradata数据,把第一个包的adts头做为配置包
          bAACExtradata = true;
          AvoxPacket adts = {};
          adts.data.bRef = true;
          adts.data.data = (uint8_t*)pkt->data;
          adts.data.size = 7;
          adts.prefixSize = 7;
          adts.packtype = (int32_t)PackType::aconfig;
          adts.index = aIndexMaps[streamId];
          adts.pts = 0;
          adts.dts = 0;
          dispatch(&IAVSourceOb::onPacket, adts);
        }
      }
    }
    // B帧可能没有pts
    if (refPkt->pts == AV_NOPTS_VALUE) {
      refPkt->pts = refPkt->dts;
    }
    // 字幕包没有有效 pts 则无法按播放时钟消费, 直接丢弃(防 NOPTS rescale 变垃圾值)
    if (packType == PackType::subtitles && refPkt->pts == AV_NOPTS_VALUE) {
      continue;
    }
    // 时间全转成毫秒; `AV_NOPTS_VALUE` 原样保留 —— av_rescale_q 会把它算成
    // "看似普通"的垃圾值(实测 INT64_MIN→INT64_MIN+1), 下游只能靠猜。
    if (refPkt->pts != AV_NOPTS_VALUE) {
      refPkt->pts = av_rescale_q(refPkt->pts, st->time_base, {1, 1000});
    }
    if (refPkt->dts != AV_NOPTS_VALUE) {
      refPkt->dts = av_rescale_q(refPkt->dts, st->time_base, {1, 1000});
    }
    if (refPkt->duration != AV_NOPTS_VALUE) {
      refPkt->duration = av_rescale_q(refPkt->duration, st->time_base, {1, 1000});
    }
    AvoxPacket packet = ffAvoxPacket(refPkt);
    packet.packtype = (int32_t)packType;
    packet.prefixSize = prefixSize;
    processPacket(packet);
    sleepTask(true, 1);
  }
  // 退出前清 seek 预读残留(线程随 close/reopen 重走 onRunTask, 不许跨开播携带)
  clearSeekStash();
}

bool IOParseFF::onOpen() {
  // 实例可能复用重开(先 close 再 open), 清掉上次的停止打断
  bStopIo = false;
  return startTask();
}

void IOParseFF::onClose() {
  // 先置打断再停线程: 阻塞中的 av_read_frame 被 interrupt_callback 截停返回
  // AVERROR_EXIT, 读循环回循环顶见 running()==false 退出, stopTask 立即 join 到
  bStopIo = true;
  stopTask();
}

void IOParseFF::pause(bool bFlag) {
  // 看协议，有些协议支持服务器暂停，这种最好
  if (fmtCtx) {
    int ret = 0;
    // ret == AVERROR(ENOSYS) 表示该协议不支持, 属正常, 忽略即可
    if (bFlag) {
      ret = av_read_pause(fmtCtx.get());
    } else {
      av_read_play(fmtCtx.get());
    }
  }
  if (bFlag) {
    pauseTask();
  } else {
    resumeTask();
  }
}

SeekType IOParseFF::seekType() const {
  if (!fmtCtx || !fmtCtx->pb) {
    return SeekType::none;
  }
  // return fmtCtx->iformat->flags & AVFMT_NOFILE;
  if ((fmtCtx->pb->seekable & AVIO_SEEKABLE_NORMAL) == AVIO_SEEKABLE_NORMAL) {
    return SeekType::normal;
  } else if ((fmtCtx->pb->seekable & AVIO_SEEKABLE_TIME) ==
             AVIO_SEEKABLE_TIME) {
    return SeekType::time;
  }
  return SeekType::none;
}

void IOParseFF::seekTo(double progress) {}

// seek 落点容差(毫秒): BACKWARD 落点=目标前最近关键帧, RM 关键帧间距可达
// 数十秒; min=target 变体实测落在目标后 1~2s, 向前容差 2s 够
static constexpr int64_t kSeekLandFwdTolMs = 2000;
static constexpr int64_t kSeekLandBackTolMs = 30000;

void IOParseFF::preSeek() {
  // 撤背压(pauseIOPacket)同时打断: 读线程失背压后第一次 av_read_frame 即被
  // interrupt 截停返回 AVERROR_EXIT → 循环顶 if(pauseing()) 持 ack 等待 seekTo.
  // 抢在狂奔到 EOF 之前
  bInterruptRead.store(true);
}

bool IOParseFF::seekTo(int64_t pos) {
  SeekType st = seekType();
  if (!fmtCtx || st == SeekType::none) {
    // preSeek 可能已置位 bInterruptRead: 早退必须清, 否则读线程被钉死在
    // EXIT→continue 空转, 且错误路径的 bInterruptRead 守卫会把真实故障的
    // recover 一起屏蔽 → 播放器挂 buffering
    bInterruptRead.store(false);
    return false;
  }
  // seek保护期: 第一个video I帧到达前不做重复GOP检测, 避免seek后I帧被误判为重复
  bSeeking = true;
  seekIdrDrops = 0;
  bool bSeek = false;
  // 无条件打断 IO 线程阻塞中的 av_read_frame 再暂停: interrupt_callback 让
  // av_read_frame 返回 AVERROR_EXIT, 读循环 continue 回循环顶 if(pauseing()) 持
  // ack 暂停. 必须打断本地也打断 —— cmdSeek 先 pauseIOPacket 撤了下游背压,
  // 若不打断读线程会狂奔到真 EOF 抢在 seek 前 onComplete → 卡死. 打断返回
  // EXIT(非 EOF), 读线程到不了 EOF 分支, 安全.
  bInterruptRead.store(true);
  pauseTask();
  // 等 IO 线程确认已退出 av_read_frame (最多约1.2s), 之后再独占 fmtCtx 做
  // seek, 避免 avformat_seek_file 与 av_read_frame 并发操作同一 fmtCtx 崩溃。
  // 上限不能缩回 200ms: 打断中的 http 协议层会走重连退避(av_usleep 1s 不可
  // 打断), 拖动进度条的 seek 风暴下 200ms 等不到 ack 是常态 → 快速 seek 门
  // (要求 bIoAcked)全关, 每条 seek 退回顺序整扫 25~50s(用户体感冻死), 且
  // 超时放行的并发 fmtCtx 操作实测撕 demuxer(Unexpected offset/Packet
  // mismatch 风暴)。1.2s 覆盖重连退避; IO 线程阻塞在下游 enqueueWait 的
  // 老桩场景仍会超时放行, 只多等 1s 不损功能
  bIoPausedAck.store(false);
  int32_t ackWait = 0;
  for (; ackWait < 60 && !bIoPausedAck.load(); ++ackWait) {
    sleepTask(false, 20);
  }
  const bool bIoAcked = bIoPausedAck.load();
  if (!bIoAcked) {
    // 静默超时是历史盲点: 读线程当时不在循环顶(多半正阻塞在下游 enqueueWait /
    // dispatch 里, 例如消费端卡住), 此时继续 avformat_seek_file 与 av_read_frame
    // 并发操作 fmtCtx 有风险, 而现场只会看到"seek 后没数据"这种无痕症状。
    // 留痕并带上等待时长/目标位置, 便于定位
    LOGFLF(LogLevel::warn, "seek: IO thread not paused in ", ackWait * 20,
           "ms (no bIoPausedAck), seeking fmtCtx anyway. pos:", pos);
  }
  // IO 线程已确认不在读, 清除打断标记, 让 avformat_seek_file 自身不被中断
  bInterruptRead.store(false);
  const int64_t target_ts = pos * 1000;
  // 上轮 seek 预读包若有残留(IO 线程停放未消费到), 先清再装填本轮校验结果
  clearSeekStash();
  // 落点校验只在本地文件做: RM/AVI 等老容器索引损坏时 avformat_seek_file
  // 落点不可信(方子传CD1 实测续播 seek 落回片头), 而纯 ffmpeg 同上下文重试
  // 即落准(seekprobe12) —— 校验不过就换变体重试; 网络流保持原行为不动
  bool bVerifyLanding =
      bIoAcked && st == SeekType::normal && sourceMode == AVSourceMode::local;
  int32_t videoStreamId = -1;
  if (bVerifyLanding) {
    for (int32_t i = 0; i < fmtCtx->nb_streams; i++) {
      auto* vs = fmtCtx->streams[i];
      if (vs->codecpar->codec_type == AVMEDIA_TYPE_VIDEO &&
          (vs->disposition & AV_DISPOSITION_ATTACHED_PIC) == 0 &&
          vs->discard != AVDISCARD_ALL) {
        videoStreamId = i;
        break;
      }
    }
    bVerifyLanding = videoStreamId >= 0;
  }
  // flv 网络流快速 seek 门(与本地落点校验互斥): demuxer=flv + 网络源。flv 的
  // read_seek 只会 avio_seek_time, 而 pb->read_seek 恒空(自建 wrapper 与原生
  // avio 都不设), 恒 ENOSYS → avformat_seek_file 退化为从当前位向前顺序整扫
  // (987s 直链实测 25~46s)。flvdec 播放期虽按关键帧/音频包自建流索引但从不
  // 自用, 这里绕过它直用索引/字节估算跳转
  bool bFlvFastSeek = false;
  if (bIoAcked && !bVerifyLanding && st == SeekType::normal &&
      sourceMode != AVSourceMode::local && fmtCtx->iformat &&
      fmtCtx->iformat->name && strcmp(fmtCtx->iformat->name, "flv") == 0) {
    for (int32_t i = 0; i < fmtCtx->nb_streams; i++) {
      auto* vs = fmtCtx->streams[i];
      if (vs->codecpar->codec_type == AVMEDIA_TYPE_VIDEO &&
          (vs->disposition & AV_DISPOSITION_ATTACHED_PIC) == 0 &&
          vs->discard != AVDISCARD_ALL) {
        videoStreamId = i;
        break;
      }
    }
    bFlvFastSeek = videoStreamId >= 0;
  }
  // 落点(ms)与关键帧标志: 本地变体纠偏与 flv 估算跳转两条路共用
  int64_t landedMs = AV_NOPTS_VALUE;
  bool sawKey = false;
  // 变体重定位: 0=-1 BACKWARD(现行) 1=-1 min=target(向后落惯用法, 落点在
  // 目标后一关键帧) 2=视频流 BACKWARD; 纯 ffmpeg 实测视频流 FORWARD 惯用法
  // 恒落文件尾, 不纳入
  auto rawSeek = [this, &pos, &videoStreamId](int32_t variant) -> bool {
    const int64_t targetUs = pos * 1000;
    int ret = 0;
    if (variant == 2 && videoStreamId >= 0) {
      auto* vs = fmtCtx->streams[videoStreamId];
      const int64_t ts = av_rescale_q(pos, {1, 1000}, vs->time_base);
      const int64_t maxTs =
          av_rescale_q(fmtCtx->duration, {1, AV_TIME_BASE}, vs->time_base);
      ret = avformat_seek_file(fmtCtx.get(), videoStreamId, INT64_MIN, ts,
                               maxTs > ts ? maxTs : INT64_MAX,
                               AVSEEK_FLAG_BACKWARD);
    } else if (variant == 1) {
      ret = avformat_seek_file(fmtCtx.get(), -1, targetUs, targetUs, INT64_MAX,
                               0);
    } else {
      ret = avformat_seek_file(fmtCtx.get(), -1, INT64_MIN, targetUs,
                               INT64_MAX, AVSEEK_FLAG_BACKWARD);
    }
    if (ret < 0) {
      AVOX_FFMEPG_LOG(ret, "avformat_seek_file failed");
      return false;
    }
    return true;
  };
  if (st == SeekType::normal) {
    if (bFlvFastSeek) {
      bSeek = flvEstimateSeek(pos, videoStreamId, landedMs, sawKey);
      if (bSeek) {
        LOGFLF(LogLevel::info, "flv fastseek landed:", landedMs, " target:",
               pos, " key:", sawKey ? 1 : 0, " stash:",
               (int32_t)seekStash.size());
      } else {
        // 估算路径失手(重同步不出/落点修不进容差): 退整扫前把读位挪到目标
        // 前约 30s 的估算字节处——整扫的兜底实现是从当前位向前顺序读, 从 0
        // 扫等于全片过网, 贴着目标前起扫一般一秒内落准
        LOGFLF(LogLevel::info, "flv fastseek fallback, landed:", landedMs,
               " target:", pos);
        clearSeekStash();
        if (fmtCtx->duration > 0) {
          const int64_t sz = avio_size(fmtCtx->pb);
          const int64_t durMs = fmtCtx->duration / 1000;
          if (sz > 0 && durMs > 0) {
            int64_t back = (int64_t)((double)(pos - 30000) * (double)sz /
                                     (double)durMs);
            back = std::max<int64_t>(0, std::min<int64_t>(back, sz - 1));
            avio_seek(fmtCtx->pb, back, SEEK_SET);
          }
        }
        avformat_flush(fmtCtx.get());
        bSeek = rawSeek(0);
      }
    } else {
      bSeek = rawSeek(0);
      // 被打断/interrupt 竞态误杀的一次性失败(同上下文重试即准)给一次重试
      if (!bSeek && bVerifyLanding) {
        bSeek = rawSeek(0);
      }
    }
  } else if (st == SeekType::time) {
    // 基于字节位置的跳转 (单位：字节)
    if (fmtCtx->pb) {
      int64_t ret =
          avio_seek_time(fmtCtx->pb, -1, target_ts, AVSEEK_FLAG_BACKWARD);
      if (ret < 0) {
        AVOX_FFMEPG_LOG(ret, "avio_seek failed");
      }
      bSeek = (ret == 0);
    }
  }
  // 落点校验+纠偏: 首个视频包 pts 偏差超容差(向前 2s/向后 30s)则换变体
  // 重试, 取偏差最小者; 始终超差也归位最优落点并留痕
  if (bSeek && bVerifyLanding) {
    const bool bLanded = verifySeekLanding(videoStreamId, landedMs, sawKey);
    const int64_t dev = bLanded ? landedMs - pos : 0;
    if (bLanded && (dev > kSeekLandFwdTolMs || dev < -kSeekLandBackTolMs)) {
      LOGFLF(LogLevel::warn, "seek mislanded, landed:", landedMs, " target:",
             pos, ", retry variants");
      int32_t bestVariant = -1;
      int32_t currentVariant = 0;
      int64_t bestAbsDev = dev < 0 ? -dev : dev;
      int64_t finalLanded = landedMs;
      bool finalKey = sawKey;
      for (int32_t v = 1; v <= 2; v++) {
        clearSeekStash();
        if (!rawSeek(v)) {
          continue;
        }
        int64_t land2 = AV_NOPTS_VALUE;
        bool key2 = false;
        if (!verifySeekLanding(videoStreamId, land2, key2)) {
          continue;
        }
        currentVariant = v;
        const int64_t dev2 = land2 - pos;
        if (dev2 <= kSeekLandFwdTolMs && dev2 >= -kSeekLandBackTolMs) {
          bestVariant = v;
          bestAbsDev = dev2 < 0 ? -dev2 : dev2;
          finalLanded = land2;
          finalKey = key2;
          break;
        }
        const int64_t abs2 = dev2 < 0 ? -dev2 : dev2;
        if (abs2 < bestAbsDev) {
          bestVariant = v;
          bestAbsDev = abs2;
          finalLanded = land2;
          finalKey = key2;
        }
      }
      // 终态归位最优落点再校验装填 stash: 基线若仍是最优, v1/v2 尝试已动过
      // demuxer, 重定+重校验; 最后一次成功尝试恰是最优则免
      if (bestVariant != currentVariant) {
        clearSeekStash();
        int64_t land3 = AV_NOPTS_VALUE;
        bool key3 = false;
        if (rawSeek(bestVariant >= 0 ? bestVariant : 0) &&
            verifySeekLanding(videoStreamId, land3, key3)) {
          finalLanded = land3;
          finalKey = key3;
        }
      }
      landedMs = finalLanded;
      sawKey = finalKey;
    }
    LOGFLF(LogLevel::info, "seek landed:", landedMs, " target:", pos,
           " key:", sawKey ? 1 : 0, " stash:", (int32_t)seekStash.size());
  }
  // 读线程若已 EOF 停放: 复位让它从新位置继续读(否则 seek 无人读, 管道静止)
  if (bSeek) {
    bEofNotified.store(false);
    bEofReset.store(true);
    // 门闸: 落点校验已在 stash 尾备好关键视频包则解除; 未校验/未见关键包
    // 照旧武装, 由读循环丢到首个 KEY 包(见 bWaitKeyframe 成员注释)
    // h264/h265 不武装: KEY 标志不可信且 AVSource nal 级 IDR 闸已覆盖,
    // 双重门闸重复扣帧(2026-09-24 seek 冻结复盘); RM 等其余编码保留
    bool bH26xStream = videoStreamId >= 0 && fmtCtx->streams[videoStreamId] &&
        (fmtCtx->streams[videoStreamId]->codecpar->codec_id == AV_CODEC_ID_H264 ||
         fmtCtx->streams[videoStreamId]->codecpar->codec_id == AV_CODEC_ID_HEVC);
    bWaitKeyframe.store(!sawKey && !bH26xStream);
    waitKeyframeDrops = 0;
  }
  // seek 落点在 wrapPb 缓冲内时 avio_seek 走快速路径不进 wrapSeekCb, 打断
  // 窗口闩进来的错误清不到, 恢复后首读即毙(seek 后不动→结束定谳②); 兜底
  // 显式清一次, 本地文件 wrapPb 为空不受影响
  if (bSeek && wrapPb) {
    wrapPb->error = 0;
    wrapPb->eof_reached = 0;
  }
  // 恢复IO线程
  resumeTask();
  if (pgsDec) {
    pgsDec->flush();
  }
  return bSeek;
}

bool IOParseFF::verifySeekLanding(int32_t videoStreamId, int64_t& landedMs,
                                  bool& sawKey, int32_t budgetMs) {
  landedMs = AV_NOPTS_VALUE;
  sawKey = false;
  if (!fmtCtx || videoStreamId < 0) {
    return false;
  }
  // 兜底上限: RM 音视频交织下首视频包就在前几个包, 256 包/1s 远够
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(budgetMs);
  AVPacketPtr pkt = getUniquePtr(av_packet_alloc());
  for (int32_t i = 0; i < 256; i++) {
    if (std::chrono::steady_clock::now() > deadline) {
      LOGFLF(LogLevel::warn, "seek landing verify timeout, packets:", i);
      break;
    }
    if (av_read_frame(fmtCtx.get(), pkt.get()) < 0) {
      break;
    }
    auto* st = fmtCtx->streams[pkt->stream_index];
    if (st->disposition & AV_DISPOSITION_ATTACHED_PIC) {
      av_packet_unref(pkt.get());
      continue;
    }
    if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO &&
        pkt->stream_index == videoStreamId) {
      if (landedMs == AV_NOPTS_VALUE && pkt->pts != AV_NOPTS_VALUE) {
        landedMs = av_rescale_q(pkt->pts, st->time_base, {1, 1000});
      }
      if (pkt->flags & AV_PKT_FLAG_KEY) {
        // 关键视频包入 stash 尾: 下游门闸可解除, 解码从 I 帧起步
        sawKey = true;
        seekStash.push_back(pkt.release());
        break;
      }
      // 非关键视频包丢弃(与门闸同语义): 校验读出的 P 帧喂解码器正是
      // 缺参考花屏源
      av_packet_unref(pkt.get());
      continue;
    }
    // 音频/字幕等包原序入 stash 回灌, 音轨开头不能缺
    seekStash.push_back(pkt.release());
    pkt = getUniquePtr(av_packet_alloc());
  }
  return landedMs != AV_NOPTS_VALUE;
}

void IOParseFF::clearSeekStash() {
  for (auto* p : seekStash) {
    av_packet_free(&p);
  }
  seekStash.clear();
}

bool IOParseFF::flvEstimateSeek(int64_t posMs, int32_t videoStreamId,
                                int64_t& landedMs, bool& sawKey) {
  landedMs = AV_NOPTS_VALUE;
  sawKey = false;
  if (!fmtCtx || !fmtCtx->pb || fmtCtx->duration <= 0 || posMs < 0) {
    return false;
  }
  const int64_t fileSize = avio_size(fmtCtx->pb);
  const int64_t durationMs = fmtCtx->duration / 1000;
  if (fileSize <= 0 || durationMs <= 0) {
    return false;
  }
  auto* vs = fmtCtx->streams[videoStreamId];
  // 索引直跳优先: flvdec 播放期给每个关键帧/音频包自建流索引但 read_seek 从
  // 不自用(恒 ENOSYS 退化整扫), 这里取目标前最近条目直接 Range 落位——回到
  // 已看过的区间时字节精确, 零修正。条目离目标超 BACKWARD 容差视为不覆盖
  // (刚起播就远跳的场景索引只有开头几个点), 走估算
  const AVIndexEntry* entry = avformat_index_get_entry_from_timestamp(
      vs, av_rescale_q(posMs, {1, 1000}, vs->time_base), AVSEEK_FLAG_BACKWARD);
  if (entry && entry->pos > 0 && entry->timestamp != AV_NOPTS_VALUE) {
    const int64_t entryMs =
        av_rescale_q(entry->timestamp, vs->time_base, {1, 1000});
    if (posMs - entryMs <= kSeekLandBackTolMs &&
        avio_seek(fmtCtx->pb, entry->pos, SEEK_SET) >= 0) {
      avformat_flush(fmtCtx.get());
      if (flvResyncTag(entry->pos) &&
          verifySeekLanding(videoStreamId, landedMs, sawKey, 8000) &&
          landedMs != AV_NOPTS_VALUE &&
          landedMs - posMs <= kSeekLandFwdTolMs) {
        return true;
      }
      clearSeekStash();
    }
  }
  const double bytesPerMs = (double)fileSize / (double)durationMs;
  // 三跳兜 VBR: 首跳按平均码率估位, 落点偏差按误差线性折算成字节修正量累计
  // 到 corrMs 再跳; 实测码率波动(本片 433MB 处偏差 -47s)一两轮内归位
  int64_t corrMs = 0;
  for (int32_t attempt = 0; attempt < 3; attempt++) {
    int64_t bytePos =
        (int64_t)((double)(posMs + corrMs) * bytesPerMs);
    bytePos = std::max<int64_t>(0, std::min<int64_t>(bytePos, fileSize - 1));
    if (avio_seek(fmtCtx->pb, bytePos, SEEK_SET) < 0) {
      return false;
    }
    // 裸挪读位后 demuxer 包缓冲/解析中间态全作废, 读前先清
    avformat_flush(fmtCtx.get());
    if (!flvResyncTag(bytePos)) {
      return false;
    }
    landedMs = AV_NOPTS_VALUE;
    sawKey = false;
    // 超时/超包数退出时 landedMs 已记首视频包位置: 修正仍可进行, 关键帧
    // 由 bWaitKeyframe 门闸兜底(见 seekTo 尾部)
    if (!verifySeekLanding(videoStreamId, landedMs, sawKey, 8000)) {
      return false;
    }
    const int64_t dev = landedMs - posMs;
    if (dev <= kSeekLandFwdTolMs && dev >= -kSeekLandBackTolMs) {
      return true;
    }
    corrMs += posMs - landedMs;
    LOGFLF(LogLevel::info, "flv fastseek correct attempt:", attempt,
           " landed:", landedMs, " target:", posMs, " corrMs:", corrMs);
  }
  return false;
}

bool IOParseFF::flvResyncTag(int64_t from) {
  if (!fmtCtx || !fmtCtx->pb) {
    return false;
  }
  constexpr int32_t kWin = 256 * 1024;
  constexpr int64_t kMaxScan = 4 * 1024 * 1024;
  std::vector<uint8_t> buf(kWin);
  int64_t base = from;
  while (base < from + kMaxScan) {
    const int n = avio_read(fmtCtx->pb, buf.data(), kWin);
    if (n < 15) {
      // 尾窗不足 prevTagSize+tag 头: 文件尾
      return false;
    }
    for (int32_t p = 0; p + 15 <= n; p++) {
      // 候选=tag 头(11B): type(1) size(3) ts(3+1) streamid(3)。tag 头自洽判定
      // = 紧随其后的 prevTagSize(4) == size+11 且 type∈{8,9,18} 且 streamid
      // 恒 0(FLV 布局 [tag][prevTagSize=size+11] 交替, 前导字段存的是上一个
      // tag 的大小, 不能拿来校验当前 tag)。32 位自洽+类型+3 零字节, 误报概率
      // 可忽略; tag 数据跨窗导致尾随字段出窗的候选放过, 由后续小 tag 命中
      const uint8_t* q = buf.data() + p;
      const uint32_t type = q[0];
      const uint32_t size =
          ((uint32_t)q[1] << 16) | ((uint32_t)q[2] << 8) | q[3];
      if ((type != 8 && type != 9 && type != 18) || size == 0 ||
          q[8] != 0 || q[9] != 0 || q[10] != 0) {
        continue;
      }
      const uint64_t end = (uint64_t)p + 11 + size;
      if (end + 4 > (uint64_t)n ||
          AV_RB32(buf.data() + end) != (uint32_t)(size + 11)) {
        continue;
      }
      // flv_read_packet 期望落在 tag 头(尾随 prevTagSize 由 leave 消费)
      avio_seek(fmtCtx->pb, base + p, SEEK_SET);
      return true;
    }
    // tag 头 11 字节可能被窗边界切开, 回退重叠续扫
    base += n - 15;
    avio_seek(fmtCtx->pb, base, SEEK_SET);
  }
  return false;
}

int64_t IOParseFF::duration() const {
  if (!fmtCtx) {
    return 0;
  }
  // FFmpeg 的 duration 单位是微秒（AV_TIME_BASE）
  const int64_t duration_us = fmtCtx->duration;
  if (duration_us == AV_NOPTS_VALUE) {
    // 直播流或无固定时长的情况
    return 0;
  }
  // 转换为毫秒（同时避免溢出）
  return duration_us / (AV_TIME_BASE / 1000);
}

double IOParseFF::progress() const {
  const int64_t dur = duration();
  if (dur <= 0) {
    return 0.0;
  }
  const int64_t pos = position();
  if (pos <= 0) {
    return 0.0;
  }
  // 计算进度并限制在合理范围
  double progress = (double)(pos) / dur;
  return std::max(0.0, std::min(progress, 1.0));
}

void IOParseFF::onSpeed() {
  // ffmpeg IO 无倍速语义(包流不变), 保持 bSpeedAble=false:
  // AVSource::setSpeed 不做 I 帧模式预判
  bSpeedAble = false;
  if (speed != 1.0) {
    // ffmpeg无发送Scale的手段(av_dict无对应选项),倍速仅zlmediakit IO支持
    LOGFLF(LogLevel::warn, "speed:", speed,
           " no support on ffmpeg io, use zlmediakit io");
  }
}

}
