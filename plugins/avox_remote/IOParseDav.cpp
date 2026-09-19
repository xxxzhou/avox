#include "IOParseDav.hpp"

#include <algorithm>
#include <cstring>

#include <httplib.h>

#include "avox/codec/H26XHelper.hpp"
#include "avox/module/LogHelper.hpp"
#include "DavSource.hpp"

namespace avox {

namespace {

// avio内部缓冲大小(单次read回调的请求上限): 与 IOParseSmb 同口径, 容器
// moov 解析的小读+seek基本落在预读窗口内
constexpr int kAvioBufferSize = 256 * 1024;
// 预读窗口大小(a05契约): 顺序读时一次 range 拉取摊薄请求次数,
// 4MB/窗口 ≈ 每 64MB 视频仅 16 个请求; seek 落窗口内零请求
constexpr uint64_t kLookaheadSize = 4 * 1024 * 1024;
// 断链自愈 (a05-T3 契约 §2): 401/403/404/410 直链级失效 → refresh 换新直链;
// 断流/5xx 瞬时类 → 退避重试同直链。次数 3 次、退避 1s/2s/4s 为契约内定值
// (option 透出后续加); httplib 不可中断 → 以「短超时+整体重试」近似
constexpr int kRecoverRetries = 3;
constexpr int kBackoffMs[kRecoverRetries] = {1000, 2000, 4000};
// 单请求粒度上限: httplib 不可中断, 打断最坏等一个请求返回,
// 拉取按窗口一次到位但超时受客户端读超时约束
// 鉴权过期等待产品重授权的窗口 (a05 §1): onAuthExpired 后 IO 层有界等待
// (30s, 与契约 closed 超时同口径), 期间缓冲冻结表现为「断流」不静默重试;
// reauthorize 落地即续播, 超时走 EIO 既有终错
constexpr int kReauthWaitMs = 30000;

std::string percentDecode(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '%' && i + 2 < s.size()) {
      char hex[3] = {s[i + 1], s[i + 2], 0};
      char* end = nullptr;
      const long v = std::strtol(hex, &end, 16);
      if (end == hex + 2) {
        out += (char)v;
        i += 2;
        continue;
      }
    }
    if (s[i] == '+') {
      out += ' ';
      continue;
    }
    out += s[i];
  }
  return out;
}

}  // namespace

IOParseDav::IOParseDav() { taskName = "dav io parse"; }

IOParseDav::~IOParseDav() { close(); }

bool IOParseDav::parseUrl(const std::string& url, UrlParts* out) {
  std::string rest = url;
  // scheme: http/https(DavSource 直链原样) 与 dav/davs(别名)
  if (url.rfind("https://", 0) == 0 || url.rfind("davs://", 0) == 0) {
    out->https = true;
    rest = url.substr(url.find("://") + 3);
  } else if (url.rfind("http://", 0) == 0 || url.rfind("dav://", 0) == 0) {
    out->https = false;
    rest = url.substr(url.find("://") + 3);
  } else {
    return false;
  }
  const size_t pathStart = rest.find('/');
  const std::string authority =
      rest.substr(0, pathStart == std::string::npos ? std::string::npos
                                                    : pathStart);
  out->path = pathStart == std::string::npos ? "" : rest.substr(pathStart);
  // userinfo: user[:pass](entryUrl 产出为百分号编码态, Basic 认证前解码)
  const size_t at = authority.rfind('@');
  std::string hostPort = authority;
  if (at != std::string::npos) {
    const std::string ui = authority.substr(0, at);
    hostPort = authority.substr(at + 1);
    const size_t colon = ui.find(':');
    if (colon == std::string::npos) {
      out->user = percentDecode(ui);
    } else {
      out->user = percentDecode(ui.substr(0, colon));
      out->pass = percentDecode(ui.substr(colon + 1));
    }
  }
  // host[:port] (IPv6 [..] 支持)
  if (!hostPort.empty() && hostPort[0] == '[') {
    const size_t rb = hostPort.find(']');
    if (rb == std::string::npos) {
      return false;
    }
    out->host = hostPort.substr(1, rb - 1);
    if (rb + 1 < hostPort.size() && hostPort[rb + 1] == ':') {
      out->port = std::atoi(hostPort.c_str() + rb + 2);
    }
  } else {
    const size_t colon = hostPort.rfind(':');
    if (colon != std::string::npos && hostPort.find(':') == colon) {
      out->host = hostPort.substr(0, colon);
      out->port = std::atoi(hostPort.c_str() + colon + 1);
    } else {
      out->host = hostPort;
    }
  }
  if (out->host.empty() || out->port < 0 || out->port > 65535) {
    return false;
  }
  if (out->port == 0) {
    out->port = out->https ? 443 : 80;
  }
  // 直链 path 必填且保持编码态(服务端按原样解析)
  return !out->path.empty();
}

bool IOParseDav::ensureClient() {
  if (client) {
    return true;
  }
  UrlParts p;
  if (!parseUrl(curUrl.empty() ? url : curUrl, &p)) {
    return false;
  }
  reqPath = p.path;
  // 旧版 httplib 无 (scheme,host,port) 三参构造, 拼 scheme_host_port 同 DavSource
  client = std::make_unique<httplib::Client>(std::string(p.https ? "https" : "http") +
                                             "://" + p.host + ":" +
                                             std::to_string(p.port));
  if (!client) {
    return false;
  }
  // 连接/读超时对齐 AVSource 注入的 timeoutMs; httplib 请求不可中断,
  // 读超时即单请求最坏阻塞上界
  const int32_t sec = std::max<int32_t>(1, (timeoutMs + 999) / 1000);
  client->set_connection_timeout(sec, 0);
  client->set_read_timeout(sec, 0);
  client->set_write_timeout(sec, 0);
  client->set_keep_alive(true);
  if (!p.user.empty()) {
    client->set_basic_auth(p.user.c_str(), p.pass.c_str());
  }
  return true;
}

int IOParseDav::fetchRange(uint64_t start, uint64_t end,
                           std::vector<uint8_t>* out) {
  if (!ensureClient() || bInterruptRead.load()) {
    return 0;
  }
  httplib::Headers headers = {{"Range", "bytes=" + std::to_string(start) +
                                            "-" + std::to_string(end)}};
  if (!authKey.empty()) {
    // 鉴权 Header 注入 (契约 §3): 断链桥重取时自会话拷贝, 直链 GET 携带
    headers.emplace(authKey, authVal);
  }
  httplib::Result res = client->Get(reqPath, headers);
  if (!res) {
    // 连接死(半开等): 重建会话重试一次
    client.reset();
    if (!ensureClient() || bInterruptRead.load()) {
      return 0;
    }
    res = client->Get(reqPath, headers);
    if (!res) {
      return 0;
    }
  }
  if (res->status != 206) {
    // 服务端不支持 range(200 全量)/鉴权失效/直链过期等, 交上层按状态分类
    LOGFLF(LogLevel::warn, "[dav io] range request status:", res->status,
           " (expect 206)");
    return res->status;
  }
  // Content-Range: bytes S-E/TOTAL 回填总大小
  auto rangeIt = res->headers.find("Content-Range");
  if (rangeIt != res->headers.end()) {
    const std::string& cr = rangeIt->second;
    const size_t slash = cr.rfind('/');
    if (slash != std::string::npos && slash + 1 < cr.size()) {
      const uint64_t total = std::strtoull(cr.c_str() + slash + 1, nullptr, 10);
      if (total > 0) {
        fileSize = total;
      }
    }
  }
  if (out) {
    // httplib body 为 string, 拷成字节缓冲
    out->assign(res->body.begin(), res->body.end());
  }
  return 206;
}

bool IOParseDav::refillWindow() {
  if (fileSize > 0 && (uint64_t)ioPosition >= fileSize) {
    return false;
  }
  const uint64_t win =
      lookaheadSize > 0 ? lookaheadSize : kLookaheadSize;
  const uint64_t start = (uint64_t)ioPosition;
  const uint64_t end =
      fileSize > 0 ? std::min(start + win, fileSize) - 1 : start + win - 1;
  // 断链自愈 (a05-T3): 首拉失败按状态分类 —— 401/403/404/410 直链级失效走
  // 断链桥 refresh 换新直链立即重试; 断流/5xx 瞬时类退避重试同直链。
  // ioPosition 在拉取失败时不前移, 恢复后从断点字节续读, PTS 无跳变;
  // 全部尝试耗尽才 false(avio EIO → 既有终错路径)
  for (int attempt = 0; attempt <= kRecoverRetries; ++attempt) {
    std::vector<uint8_t> data;
    const int status = fetchRange(start, end, &data);
    if (bInterruptRead.load()) {
      return false;
    }
    if (status == 206 && !data.empty()) {
      winBuf = std::move(data);
      winStart = start;
      winValid = winBuf.size();
      return true;
    }
    if (attempt >= kRecoverRetries) {
      break;
    }
    const bool linkBroken =
        (status == 401 || status == 403 || status == 404 || status == 410);
    LOGFLF(LogLevel::warn, "[dav io] range fetch failed status:", status,
           linkBroken ? " -> refresh direct link"
                      : " -> transient backoff retry",
           " attempt:", attempt + 1);
    if (linkBroken) {
      if (bridgeRefresh()) {
        client.reset();  // 新直链(换签名/凭据), 下次 ensureClient 按新 URL 重建
        continue;        // 立即重试, 不退避
      }
      // refresh 失败且会话已置鉴权过期态 (a05 §1): onAuthExpired 已由会话抛出,
      // 有界等待产品 reauthorize (轮询过期态清除, 不发请求不打服务器), 落地后
      // 断链桥以新凭据换链续播; 超时/无桥(裸 URL 播放)按断流终错
      if (waitForReauth() && bridgeRefresh()) {
        client.reset();
        continue;
      }
      LOGFLF(LogLevel::error, "[dav io] refresh unavailable, give up");
      return false;
    }
    if (!backoffSleep(attempt)) {
      return false;
    }
  }
  return false;
}

bool IOParseDav::backoffSleep(int retry) {
  const int totalMs =
      (retry >= 0 && retry < kRecoverRetries) ? kBackoffMs[retry] : 1000;
  // 分片轮询: seek/close 打断退避立即让位
  for (int waited = 0; waited < totalMs; waited += 50) {
    if (bInterruptRead.load()) {
      return false;
    }
    sleepTask(false, 50);
  }
  return true;
}

bool IOParseDav::bridgeRefresh() {
  DavSource* src = davbridge::findForUrl(url);
  if (src == nullptr) {
    return false;
  }
  std::string fresh;
  if (!src->refreshPlaybackUrl(&fresh)) {
    return false;
  }
  curUrl = fresh;
  src->playbackAuthHeader(&authKey, &authVal);
  LOGFLF(LogLevel::info, "[dav io] refreshed direct link, len:",
         (double)fresh.size());
  return true;
}

bool IOParseDav::waitForReauth() {
  // a05 §1: 鉴权过期后等待产品 reauthorize, 轮询会话过期态(500ms, 有界 30s),
  // 不发请求不打服务器; seek/close 打断随时退。true = 过期态已清除, 可再过桥
  for (int waited = 0; waited < kReauthWaitMs; waited += 500) {
    if (bInterruptRead.load()) {
      return false;
    }
    sleepTask(false, 500);
    DavSource* src = davbridge::findForUrl(url);
    if (src == nullptr) {
      return false;  // 会话已析构
    }
    if (!src->authExpired()) {
      return true;
    }
  }
  LOGFLF(LogLevel::warn, "[dav io] reauthorize wait timeout(ms):",
         (double)kReauthWaitMs);
  return false;
}

// ---- avio回调: FFmpeg读请求从预读窗口供给 ----
int IOParseDav::ioReadPacket(void* opaque, uint8_t* buf, int bufSize) {
  IOParseDav* self = static_cast<IOParseDav*>(opaque);
  if (!self) {
    return AVERROR_EXIT;
  }
  if (self->bInterruptRead.load()) {
    // seek/close打断窗口: 立即让出(av_read_frame收到EXIT回循环顶处理)
    return AVERROR_EXIT;
  }
  if (self->fileSize > 0 && (uint64_t)self->ioPosition >= self->fileSize) {
    return AVERROR_EOF;
  }
  // 窗口未覆盖当前游标: 重新拉取(顺序命中/seek后首次miss各一次请求)
  if ((uint64_t)self->ioPosition < self->winStart ||
      (uint64_t)self->ioPosition >= self->winStart + self->winValid) {
    if (!self->refillWindow()) {
      return self->bInterruptRead.load() ? AVERROR_EXIT : AVERROR(EIO);
    }
  }
  const uint64_t offset = (uint64_t)self->ioPosition - self->winStart;
  const int got = (int)std::min<uint64_t>(bufSize, self->winValid - offset);
  if (got <= 0) {
    return AVERROR_EOF;
  }
  std::memcpy(buf, self->winBuf.data() + offset, got);
  self->ioPosition += got;
  return got;
}

int64_t IOParseDav::ioSeek(void* opaque, int64_t offset, int whence) {
  IOParseDav* self = static_cast<IOParseDav*>(opaque);
  if (!self) {
    return -1;
  }
  const uint64_t size = self->fileSize;
  // 尺寸查询: 不移动游标
  if (whence & AVSEEK_SIZE) {
    return size > 0 ? (int64_t)size : -1;
  }
  int64_t target = 0;
  switch (whence) {
    case SEEK_SET:
      target = offset;
      break;
    case SEEK_CUR:
      target = self->ioPosition + offset;
      break;
    case SEEK_END:
      target = (int64_t)size + offset;
      break;
    default:
      return -1;
  }
  if (target < 0) {
    target = 0;
  }
  if (size > 0 && (uint64_t)target > size) {
    target = (int64_t)size;
  }
  self->ioPosition = target;
  // 窗口命中保持(免请求); miss 时下次 read 自动重拉
  return target;
}

int IOParseDav::decodeInterruptCb(void* ctx) {
  IOParseDav* self = static_cast<IOParseDav*>(ctx);
  if (!self) {
    return 1;
  }
  // seek窗口内打断in-flight读(av_read_frame阻塞中)
  return self->interruptIo() ? 1 : 0;
}

bool IOParseDav::parseStream(int32_t streamId,
                             AVCodecParameters* codecpar) {
  if (codecpar->codec_type != AVMEDIA_TYPE_VIDEO &&
      codecpar->codec_type != AVMEDIA_TYPE_AUDIO) {
    return true;
  }
  if (!bDisableVideo && codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
    if (codecpar->codec_id == AV_CODEC_ID_H264 ||
        codecpar->codec_id == AV_CODEC_ID_H265) {
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
  if (!bDisableAudio && codecpar->codec_type == AVMEDIA_TYPE_AUDIO &&
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
    }
  }
  return true;
}

void IOParseDav::parseH26xConfig(int32_t streamId,
                                 const uint8_t* extradata, int32_t size,
                                 AVCodecID codecId) {
  // 与IOParseFF同构: annexb直播格式 / avcc-hvcc本地格式
  std::vector<PacketBuf> packets;
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
    AvoxPacket spsPps = {};
    spsPps.data.bRef = true;
    spsPps.data.data = packet.buff.data();
    spsPps.data.size = packet.size;
    spsPps.prefixSize = packet.prefixSize;
    spsPps.pts = 0;
    spsPps.dts = 0;
    spsPps.packtype = (int32_t)PackType::vconfig;
    spsPps.index = vIndexMaps[streamId];
    dispatch(&IAVSourceOb::onPacket, spsPps);
    updateConfig(spsPps);
  }
}

void IOParseDav::parseAACConfig(int32_t streamId,
                                const uint8_t* extradata, int32_t size) {
  if (size < 2) {
    return;
  }
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
  bAACExtradata = true;
}

bool IOParseDav::onOpen() {
  UrlParts p;
  if (!parseUrl(url, &p)) {
    dispatch(&IAVSourceOb::onError, AVError::urlNoSupport,
             "unsupported url(expect http(s)|dav(s)://[user:pass@]host[:port]/path)");
    return false;
  }
  curUrl = url;  // 生效直链初值; refresh 重取后由 bridgeRefresh 更新
  if (!ensureClient()) {
    dispatch(&IAVSourceOb::onError, AVError::urlNoSupport, "http client init failed");
    return false;
  }
  // 探测文件大小(bytes=0-0, 1字节body): Content-Range total 回填 fileSize,
  // 同时验证服务端支持 range(不支持时明确报错, 不做全量渐进降级)
  std::vector<uint8_t> probe;
  if (fetchRange(0, 0, &probe) != 206 || fileSize == 0) {
    dispatch(&IAVSourceOb::onError, AVError::urlNoSupport,
             "range probe failed(server may not support range requests)");
    client.reset();
    return false;
  }
  ioPosition = 0;
  winStart = 0;
  winValid = 0;
  // 自定义avio上下文: read/seek回调对接http range, seekable=NORMAL使
  // mov/mkv解容器按可seek文件处理(duration索引可用)
  ioBuffer = (unsigned char*)av_malloc(kAvioBufferSize);
  ioCtx = avio_alloc_context(ioBuffer, kAvioBufferSize, 0, this, ioReadPacket,
                             nullptr, ioSeek);
  ioCtx->seekable = AVIO_SEEKABLE_NORMAL;
  LOGFLF(LogLevel::info, "[dav io] opening size:",
         (double)(fileSize / 1024 / 1024), "MB");
  return startTask();
}

void IOParseDav::onRunTask() {
  // 打开流: CUSTOM_IO由本类供pb, avformat只做探测+解封装
  AVFormatContext* temp = avformat_alloc_context();
  temp->pb = ioCtx;
  temp->flags |= AVFMT_FLAG_CUSTOM_IO;
  temp->interrupt_callback.callback = decodeInterruptCb;
  temp->interrupt_callback.opaque = this;
  int ret = avformat_open_input(&temp, "", nullptr, nullptr);
  if (ret < 0) {
    AVOX_FFMEPG_LOG(ret, "avformat_open_input failed")
    fmtCtx.reset();
    releaseIoContext();
    dispatch(&IAVSourceOb::onError, ffIoError(ret), "open input failed");
    return;
  }
  fmtCtx = getUniquePtr(temp);
  if ((ret = avformat_find_stream_info(fmtCtx.get(), nullptr)) < 0) {
    AVOX_FFMEPG_LOG(ret, "avformat_find_stream_info failed")
    dispatch(&IAVSourceOb::onError, ffIoError(ret), "open stream failed");
    return;
  }
  // 流描述(对齐IOParseFF)
  for (int32_t i = 0; i < fmtCtx->nb_streams; i++) {
    auto& st = fmtCtx->streams[i];
    // 跳过封面图等附加静态图流
    if (st->disposition & AV_DISPOSITION_ATTACHED_PIC) {
      continue;
    }
    if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && !bDisableVideo) {
      VTrackDesc vdesc = {};
      vdesc.codecId = ffVCodec(st->codecpar->codec_id);
      if (vdesc.codecId == VCodecId::none) {
        LOGFLF(LogLevel::warn, "[dav io] unsupported video codec:",
               st->codecpar->codec_id);
        continue;
      }
      vdesc.trackId = st->index;
      vdesc.desc.width = st->codecpar->width;
      vdesc.desc.height = st->codecpar->height;
      vdesc.desc.fps = ffFps(st);
      vdesc.desc.type = ffYuvType((AVPixelFormat)st->codecpar->format);
      addVideoDesc(vdesc);
    } else if (st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO &&
               !bDisableAudio) {
      ATrackDesc adesc = {};
      adesc.codecId = ffACodec(st->codecpar->codec_id);
      if (adesc.codecId == ACodecId::none) {
        LOGFLF(LogLevel::warn, "[dav io] unsupported audio codec:",
               st->codecpar->codec_id);
        bDisableAudio = true;
        continue;
      }
      adesc.trackId = st->index;
      adesc.desc.sampleRate = st->codecpar->sample_rate;
      adesc.desc.format = ffAudioFromat((int32_t)st->codecpar->format);
      adesc.desc.channels = st->codecpar->ch_layout.nb_channels;
      adesc.desc.blockAlign = st->codecpar->block_align;
      addAudioDesc(adesc);
      audioDesc = adesc.desc;
    }
  }
  // 无任何可播轨道(编码不支持): 快速报错而非让上层无限等
  if (getVideoTracks().empty() && getAudioTracks().empty()) {
    LOGFLF(LogLevel::error, "[dav io] no playable codec track, url:", url);
    releaseIoContext();
    dispatch(&IAVSourceOb::onError, AVError::urlNoSupport,
             "no playable codec track(编码不支持)");
    return;
  }
  SeekType stype = seekType();
  bSeek = false;
  if (stype != SeekType::none) {
    bSeek = true;
  }
  // range 文件是完整可seek文件: 有总时长、进度可视
  sourceMode = fmtCtx->duration > 0 ? AVSourceMode::local : AVSourceMode::live;
  trackReady();
  // 派发各流配置包(SPS/PPS/VPS/ASC)
  for (int32_t i = 0; i < fmtCtx->nb_streams; i++) {
    auto& st = fmtCtx->streams[i];
    parseStream(i, st->codecpar);
  }
  // 读循环: 与IOParseSmb/IOParseFF逐行同构(暂停确认/interrupt打断/EOF/ADTS提升)
  while (running()) {
    if (pauseing()) {
      // 已退出av_read_frame, 通知seekTo可以安全操作fmtCtx
      bIoPausedAck.store(true);
      sleepTask(false, 10);
      continue;
    }
    if (bEof.load()) {
      // EOF 停放: 不 break。小文件起播即读完, 尾包可能还有几十秒数据没被
      // 消费, 且之后很可能 seek —— 线程一退, seek 只挪了 demuxer 位置却
      // 无人再读, 管道从此静止。等 seekTo 复位(bEofReset)或 close
      if (bEofReset.exchange(false)) {
        bEof.store(false);
        continue;
      }
      sleepTask(false, 10);
      continue;
    }
    AVPacketPtr pkt = getUniquePtr(av_packet_alloc());
    ret = av_read_frame(fmtCtx.get(), pkt.get());
    if (ret < 0) {
      if (ret == AVERROR_EOF) {
        bEof.store(true);
        if (!bEofNotified.exchange(true)) {
          dispatch(&IAVSourceOb::onComplete);
        }
        continue;
      }
      // interrupt打断或非阻塞暂无数据: 回循环顶等暂停处理
      if (ret == AVERROR_EXIT || ret == AVERROR(EAGAIN)) {
        sleepTask(false, 1);
        continue;
      }
      // seek打断窗口内的INVALIDDATA是截停副产物, 不算故障
      if (bInterruptRead.load()) {
        continue;
      }
      // 真实IO错误(网络断开等)
      AVOX_FFMEPG_LOG(ret, "read frame failed")
      dispatch(&IAVSourceOb::onError, ffIoError(ret), "read frame failed");
      break;
    }
    int32_t streamId = pkt->stream_index;
    auto& st = fmtCtx->streams[streamId];
    if (st->disposition & AV_DISPOSITION_ATTACHED_PIC) {
      continue;
    }
    PackType packType = PackType::other;
    if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
      packType = PackType::video;
      if (bDisableVideo) {
        continue;
      }
    } else if (st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
      packType = PackType::audio;
      if (bDisableAudio) {
        continue;
      }
    }
    if (packType == PackType::other) {
      continue;
    }
    int32_t prefixSize = 0;
    if (packType == PackType::video) {
      prefixSize = 4;
    } else if (st->codecpar->codec_id == AV_CODEC_ID_AAC &&
               bAdtsHeader(pkt->data, pkt->size) && !bAACExtradata) {
      // AAC裸流且无extradata时把首个ADTS头提升为配置包
      bAACExtradata = true;
      AvoxPacket adts = {};
      adts.data.bRef = true;
      adts.data.data = pkt->data;
      adts.data.size = 7;
      adts.prefixSize = 7;
      adts.packtype = (int32_t)PackType::aconfig;
      adts.index = aIndexMaps[streamId];
      adts.pts = 0;
      adts.dts = 0;
      dispatch(&IAVSourceOb::onPacket, adts);
    }
    // B帧可能没有pts, 用dts补齐后统一转毫秒
    if (pkt->pts == AV_NOPTS_VALUE) {
      pkt->pts = pkt->dts;
    }
    pkt->pts = av_rescale_q(pkt->pts, st->time_base, {1, 1000});
    pkt->dts = av_rescale_q(pkt->dts, st->time_base, {1, 1000});
    pkt->duration = av_rescale_q(pkt->duration, st->time_base, {1, 1000});
    AvoxPacket packet = ffAvoxPacket(pkt.get());
    packet.packtype = (int32_t)packType;
    packet.prefixSize = prefixSize;
    processPacket(packet);
    sleepTask(true, 1);
  }
}

void IOParseDav::releaseIoContext() {
  // 手动构建的pb自行释放(avio内部缓冲可能已realloc替换, 取当前指针释放)
  if (ioCtx) {
    av_freep(&ioCtx->buffer);
    av_freep(&ioCtx);
  } else if (ioBuffer) {
    av_freep(&ioBuffer);
  }
  ioBuffer = nullptr;
  ioCtx = nullptr;
}

void IOParseDav::onClose() {
  // 先置打断再停线程: IO线程可能阻塞在http请求等待里,
  // 靠bInterruptRead+读超时快速退出, stopTask的join才能及时返回
  bInterruptRead.store(true);
  stopTask();
  fmtCtx.reset();
  releaseIoContext();
  client.reset();
  winBuf.clear();
  winStart = 0;
  winValid = 0;
  bEof.store(false);
  bEofNotified.store(false);
  bEofReset.store(false);
  bInterruptRead.store(false);
}

void IOParseDav::preSeek() { bInterruptRead.store(true); }

void IOParseDav::pause(bool bFlag) {
  if (bFlag) {
    pauseTask();
  } else {
    resumeTask();
  }
}

SeekType IOParseDav::seekType() const {
  if (!fmtCtx || !fmtCtx->pb) {
    return SeekType::none;
  }
  if ((fmtCtx->pb->seekable & AVIO_SEEKABLE_NORMAL) == AVIO_SEEKABLE_NORMAL) {
    return SeekType::normal;
  }
  return SeekType::none;
}

void IOParseDav::seekTo(double progress) {}

bool IOParseDav::seekTo(int64_t pos) {
  SeekType st = seekType();
  if (!fmtCtx || st == SeekType::none) {
    // 早退必须清标记, 否则读线程被钉死在EXIT空转(见IOParseFF同名注释)
    bInterruptRead.store(false);
    return false;
  }
  bSeeking = true;
  bool bOk = false;
  // 无条件打断IO线程的av_read_frame并等待确认, 再独占fmtCtx执行seek
  bInterruptRead.store(true);
  pauseTask();
  bIoPausedAck.store(false);
  for (int i = 0; i < 10 && !bIoPausedAck.load(); ++i) {
    sleepTask(false, 20);
  }
  bInterruptRead.store(false);
  const int64_t targetTs = pos * 1000;
  int ret = avformat_seek_file(fmtCtx.get(), -1, INT64_MIN, targetTs,
                               INT64_MAX, AVSEEK_FLAG_BACKWARD);
  if (ret < 0) {
    AVOX_FFMEPG_LOG(ret, "avformat_seek_file failed")
  }
  bOk = (ret == 0);
  // 读线程若已 EOF 停放: 复位让它从新位置继续读(否则 seek 无人读, 管道静止)
  if (bOk) {
    bEofNotified.store(false);
    bEofReset.store(true);
  }
  resumeTask();
  return bOk;
}

int64_t IOParseDav::duration() const {
  if (!fmtCtx) {
    return 0;
  }
  const int64_t durUs = fmtCtx->duration;
  if (durUs == AV_NOPTS_VALUE) {
    return 0;
  }
  return durUs / (AV_TIME_BASE / 1000);
}

double IOParseDav::progress() const {
  const int64_t dur = duration();
  if (dur <= 0) {
    return 0.0;
  }
  const int64_t pos = position();
  if (pos <= 0) {
    return 0.0;
  }
  double ratio = (double)pos / (double)dur;
  return std::max(0.0, std::min(ratio, 1.0));
}

void IOParseDav::onOptionChange(const char* key, ArgType type) {
  // 插件本地键: 预读窗口字节数(播放中调整下一窗生效; linkOption 建链时还会
  // 重放既有键, open 前设置的值在这里收到)。经 option 成员上转 IOption* 取值
  // (getLink 未导出, 见 onOpen 同注)。
  // ⚠ 只准在变更回调里取键值, 不准对可能不存在的键主动 getString ——
  // 本仓 JsonOption 对缺失键的读取行为未定义, 已实测进程硬死(exit 127)
  if (std::strcmp(key, "remote.dav.lookahead") == 0) {
    if (option != nullptr) {
      IOption* io = option;
      const char* v = io->getString(key);
      if (v != nullptr && *v != '\0') {
        const unsigned long long n = std::strtoull(v, nullptr, 10);
        if (n > 0) {
          lookaheadSize = (uint64_t)n;
        }
      }
    }
    (void)type;
    return;
  }
  // 其余交给基类(timeout等)
  AVSource::onOptionChange(key, type);
}

}
