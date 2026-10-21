#include "VideoTrack.hpp"

#include <cstdio>

#include "../module/AvoxManager.hpp"
#include "../video/VideoDecoder.hpp"
#include "MediaPlayer.hpp"

namespace avox {

namespace {
// 帧入口大跳门限: 重排乱序/爆发戳都是帧级小跳(实测 ≤100ms 级), 超过即判 seek 重入
constexpr int64_t kFrameJumpMs = 1000;
}  // namespace

VideoTrack::VideoTrack() {
  // 硬解下队列过大易花屏(帧资源大, 超出解码队列时索引可能已被释放)
  frameQueue.setMaxSize(10);
  trackType = TrackType::video;
  decodeTask = std::make_unique<VDecoderTask>();
  windowRender = std::make_unique<WindowRender>();
}

VideoTrack::~VideoTrack() {}

void VideoTrack::setTrackDesc(const VTrackDesc& trackDesc) {
  setTrackId(trackDesc.trackId);
  codecId = trackDesc.codecId;
  srcDesc = trackDesc.desc;
  // 容器 DOVI conf: 透传给解码器侧的元数据提取器(硬解腿自扫 RPU 时用)
  dvProfile = trackDesc.dvProfile;
  // 如果没有fps，默认25
  if (srcDesc.fps == 0) {
    srcDesc.fps = 25;
  }
  if (codecId == VCodecId::none) {
    LOGFLF(LogLevel::warn, "unsupported codec ", (int32_t)codecId);
    return;
  }
  onInitDesc();
  frameQueue.setClose(false);
}

void VideoTrack::start() {
  windowRender->setFrameSource(this);
  windowRender->start();
  bool bInit = decodeTask->start(this);
  bHardDecode = decodeTask->hardDecode();
  if (!bInit) {
    log(LogLevel::warn, "decodeTask start failed");
    return;
  }
  if (mediaPlayer) {
    // 统一字幕视图(文本与轨通道同一对象): 重开循环后重挂渲染回调
    SubtitleView* subtitleView = mediaPlayer->getSubtitleView();
    if (subtitleView) {
      subtitleView->setWindowRender(windowRender.get());
      subtitleView->setStorageSize(srcDesc.width, srcDesc.height);
    }
  }
  log(LogLevel::info, "decode and render start success");
}

void VideoTrack::onVideoDesc() {
  // 每次开流复位: track跨open复用, 不复位则第二个HDR流不再回调
  bHdrMetaSent = false;
  // 每次开流复位 DV 整形元数据: 解码器只在帧带 DOVI side data 时派发, 非 DV 流
  // 整片不派发 —— 不复位则渲染腿沿用上个 DV 片的整形链(doviEnable 恒 1), 非 DV
  // 内容被当 IPT 处理, 整体偏色(DV 片之后播 HDR10 片发红的根因)
  windowRender->setDoviMeta(DoviMeta{});
  // 设置图像格式
  ImageFormat imageFormat = {};
  imageFormat.width = srcDesc.width;
  imageFormat.height = srcDesc.height;
  // 记录视频编码信息
  const DecoderParams& dparams = decodeTask->getDecoder()->getDecoderParams();
  // 优先使用解码器配置
  if (dparams.width > 0 && dparams.height > 0) {
    imageFormat.width = dparams.width;
    imageFormat.height = dparams.height;
  }
  windowRender->setFPS(dparams.fps);
  dropDuration = (int32_t)(1000.0 / windowRender->getFPS()) * 2;
  // 钳位步长用流自身fps, 与渲染器解耦(渲染侧fps可能被改写)
  double clampFps = dparams.fps > 1.0 ? dparams.fps : srcDesc.fps;
  nominalFrameMs = clampFps > 1.0 ? (int64_t)(1000.0 / clampFps) : 40;
  // 字幕画布坐标系 = srcDesc(与 attachSubtitle 的轨通道 storage 同源);
  // 不用 dparams——部分硬解路径上报的高度非视频真实高度(如 1080p 报 270),
  // 会导致文本字号按错的比例缩放
  // 按流下发颜色空间(矩阵+量程+transfer): 图未建时 VkVideoRender 存成员, 建图时应用
  windowRender->setColorSpace(srcDesc.colorSpace);
  // 记录track里的渲染器创建
  PBMediaAction pb = {};
  pb.mediaObject = MediaObject::render;
  pb.trackType = trackType;
  pb.action = MediaAction::create;
  pushPB<MPPBType::MediaAction>(mpPingback, pb);
}

void VideoTrack::onPacket(PacketBufPtr packet) { pullPacket(packet); }

void VideoTrack::onHdrMeta(const HdrMeta& hdrMeta) {
  // 峰值相关(L1/CLL/mastering)变化才转发: DV 场景切换静态三元组不变、仅 L1 变,
  // 一次闸会吞掉动态膝点; 上游 updateHdrMeta 已按同字段去重, 此处防重复派发
  const bool bHold = bHdrMetaSent &&
                     lastHdrMeta.l1MaxNits == hdrMeta.l1MaxNits &&
                     lastHdrMeta.maxCLL == hdrMeta.maxCLL &&
                     lastHdrMeta.maxLuminance == hdrMeta.maxLuminance;
  // 探针走 stderr: playtest 环境 logTask 启动后不再排水, 引擎 info 日志不可见
  fprintf(stderr, "[hdr] track l1max=%.1f cll=%u fwd=%d\n",
          (double)hdrMeta.l1MaxNits, hdrMeta.maxCLL, (int)!bHold);
  if (bHold) {
    return;
  }
  lastHdrMeta = hdrMeta;
  bHdrMetaSent = true;
  windowRender->setHdrMeta(hdrMeta);
  // 宿主观察面: 流级媒体属性, 供HDR标识/显示模式切换/setHdrMode决策
  if (mediaPlayer) {
    mediaPlayer->Observer<IMediaPlayerOb>::dispatch(&IMediaPlayerOb::onHdrMeta,
                                                    hdrMeta);
  }
}

void VideoTrack::onDoviMeta(const DoviMeta& doviMeta) {
  // 探针走 stderr: playtest 环境 logTask 启动后不再排水, 引擎 info 日志不可见
  fprintf(stderr, "[dovi] dispatch valid=%d pivots=%d/%d/%d\n",
          (int)doviMeta.valid, (int)doviMeta.comp[0].numPivots,
          (int)doviMeta.comp[1].numPivots,
          (int)doviMeta.comp[2].numPivots);
  // 上游已按 memcmp 去重(场景粒度), 直接双路转发
  windowRender->setDoviMeta(doviMeta);
}

// B帧重排输出会把包级改写过的pts按乱序吐出(best_effort_timestamp取自
// dts链), 容器爆发戳(RM成对1ms)则过密; 两者都让duration变负/过小,
void VideoTrack::restampFramePts(int64_t& pts) {
  if (pts == AVOX_NOVALID_PTS) {
    return;
  }
  if (lastInPts != AVOX_NOVALID_PTS) {
    const int64_t delta = pts - lastInPts;
// seek 重入: 大跳(两个方向)远超任何重排/爆发戳, 直接以本帧为准重开格。
// 缺此判据时后向 seek 会被当成倒跳: 旧位置残留帧的 pts 大于目标位, 逃过
    if (delta > kFrameJumpMs || delta < -kFrameJumpMs) {
      lastInPts = pts;
      return;
    }
    if (pts < lastInPts + std::max<int64_t>(1, nominalFrameMs / 2)) {
      pts = lastInPts + nominalFrameMs;
    }
  }
  lastInPts = pts;
}

void VideoTrack::onDecode(const YUVFrame& frame) {
  // 压入Frame队列,这里队列如果满了，会一直阻塞解码线程
  // Packet不能覆盖，但是帧应该是覆盖过去
  // 直播应该是覆盖，而本地播放应该是等待？
  // 因为直播过来IO与播放应该是相同的，而本地IO会很快
  YUVFrame out = frame;
  // 精确seek: 目标位前的帧静默丢弃不入队(restamp前判, flush已复位lastInPts);
  // B帧重排乱序安全——目标前全丢, 首个>=目标位的帧恢复入队
  if (seekDiscardPts.load() != AVOX_NOVALID_PTS) {
    if (out.pts < seekDiscardPts.load()) {
      return;
    }
    seekDiscardPts = AVOX_NOVALID_PTS;
  }
  restampFramePts(out.pts);
  frameQueue.enqueueWait<YUVFrame>(out, copyBufHost);
  // frameQueue.enqueue<YUVFrame>(frame, copyBufHost, true);
  logDecode(out.pts, out.keyFrame);
}

void VideoTrack::onDecodeGpu(const GpuFrame& frame) {
  GpuFrame out = frame;
  // 精确seek: 同onDecode, 软硬解两条入口同一丢弃判据
  if (seekDiscardPts.load() != AVOX_NOVALID_PTS) {
    if (out.pts < seekDiscardPts.load()) {
      return;
    }
    seekDiscardPts = AVOX_NOVALID_PTS;
  }
  restampFramePts(out.pts);
  frameQueue.enqueueWait<GpuFrame>(out, copyBufGpu);
  // frameQueue.enqueue<YUVFrame>(frame, copyBufGpu, true);
  logDecode(out.pts, out.keyFrame);
}

void VideoTrack::onVideoComplete() {
  bDecodeEnd = true;
  // 记录解码结束
  PBMediaAction pb = {};
  pb.mediaObject = MediaObject::decode;
  pb.trackType = trackType;
  pb.action = MediaAction::complete;
  pushPB<MPPBType::MediaAction>(mpPingback, pb);
}

double VideoTrack::getFps() { return fpsCounter.value(); }

int64_t VideoTrack::onWinUpdate() {
  // 处理帧数据，这个Frame不好提取出来
  // 因为frame如果不在队列锁里，可能异步访问时有问题
  // 比如外面在清除队列，这时引发解码器释放队列里GPU数据
  // 然后当前又在窗口线程访问GPU数据，就会出问题
  // 但是这个锁导致别的线程访问队列锁信息时，同步代价大
  auto frameAction = [&](const VideoFramePtr& frame) {
    windowRender->render(*frame);
  };
  // 确定是否需要刷新画画
  SyncResult syncResult = syncVideo();
  // LOGFLF(LogLevel::info, "syncType ", getSyncResultStr(syncResult));
  if (syncResult == SyncResult::none || syncResult == SyncResult::slow) {
    //  FrameQueue里的线程锁下执行
    bool bGet = getFrameQueue().dequeueAction(frameAction);
    if (bGet && mediaPlayer) {
      RawMuxer* rawMuxer = mediaPlayer->getRawMuxer();
      if (rawMuxer && rawMuxer->getState() == RecorderState::recording) {
        YUVFrame yframe = {};
        if (windowRender->getCpuFrame(yframe)) {
          rawMuxer->pushFrame(yframe);
        }
      }
    }
    onFrameResult(bGet);
  }
  windowRender->setSyncResult(syncResult);

  // 同步二者的速度
  if (windowRender->getSpeed() != clock->getSpeed()) {
    windowRender->setSpeed(clock->getSpeed());
  }
  return nextDueMs;
}

WindowRender* VideoTrack::getSurfaceRender() { return windowRender.get(); }

void VideoTrack::setResetDecoderFlag() {
  if (decodeTask) {
    decodeTask->setResetFlag();
  }
}

void VideoTrack::onResetDecoder() {
  // 不同解码器，帧队列可能不同，清空原队列
  frameQueue.clear();
  clock->reset();
  // 当前在解码器线程中，转到播放器线程中重开解码器
  if (mediaPlayer) {
    mediaPlayer->onResetDecoder();
  }
}

// compute_target_delay
int64_t VideoTrack::computeDelay(int64_t delay) {
  if (!mediaPlayer) {
    return delay;
  }
  if (mediaPlayer->getSyncType() == SyncType::none) {
    return delay;
  }
  Clock* mainClock = mediaPlayer->getMainClock();
  // 如果不是主时钟，需要与主时钟比较差异
  if (clock.get() != mainClock) {
    int64_t sClock = clock->clock();
    mClock = mainClock->clock();
    if (sClock == AVOX_NOVALID_PTS || mClock == AVOX_NOVALID_PTS) {
      return delay;
    }
    // 视频与主时钟的差值，正表明视频快，负表明视频慢
    const int64_t inst = sClock - mClock;
    // 主钟是“写入 PTS - 队列时长”的估算, 自身有 ±百 ms 抖动: 直接按瞬时差
    // 纠正会让画面在 0/双倍帧距间来回跳(实测 41→83→166), 先低通再每帧微调
    if (!syncDiffInit) {
      syncDiffFiltMs = inst;
      syncDiffInit = true;
    } else {
      syncDiffFiltMs += (inst - syncDiffFiltMs) / 8;
    }
    if (delay > 0) {
      const int64_t base = delay;
      const int64_t dead = std::max<int64_t>(30, base / 2);
      const int64_t step = std::max<int64_t>(1, base / 3);
      if (syncDiffFiltMs > dead) {
        // 视频超前: 本帧多停一点, 不整帧丢弃
        delay += std::min(syncDiffFiltMs, step);
      } else if (syncDiffFiltMs < -dead) {
        // 视频落后: 本帧少停一点, 但不连发两帧
        delay -= std::min(-syncDiffFiltMs, step);
      }
      delay = std::min(base + base / 2,
                       std::max(std::max<int64_t>(5, base / 2), delay));
    }
  }
  return delay;
}

// ffplay video_refresh
// -1 视频帧慢了,丢弃,无效帧 0 同步帧 1 视频帧快了 2 队列没帧了
SyncResult VideoTrack::syncVideo() {
  // TEMP 取证探针(定位安卓起播卡顿, 定案后删): 5 秒心跳 + 长等/断粮现场
  int64_t pNow = timeStampMS();
  static int64_t pLastFrameMs = 0, pLastStatMs = 0, pMaxWait = 0, pMaxNoData = 0;
  static int64_t pLastRenMs = 0;
  static int32_t pRen = 0, pQuick = 0, pSlow = 0, pNo = 0;
  if (!pLastStatMs) pLastStatMs = pNow;
  if (pNow - pLastStatMs >= 5000) {
    log(LogLevel::info, "[pace stat] ren:", pRen, " quick:", pQuick, " slow:", pSlow,
        " nodata:", pNo, " maxwait:", (int32_t)pMaxWait,
        " maxnodata:", (int32_t)pMaxNoData, " q:", (int32_t)frameQueue.size(),
        " mclk:", mClock);
    pLastStatMs = pNow;
    pRen = pQuick = pSlow = pNo = 0;
    pMaxWait = pMaxNoData = 0;
  }
  // 取队列的最前面frame的数据，但是不弹出
  VideoFramePtr frame = nullptr;
  bool bGet = frameQueue.peek(frame);
  if (!bGet) {
    int64_t idle = pLastFrameMs ? pNow - pLastFrameMs : 0;
    // 限速: 断粮时本函数每 5ms 被调一次, 不限速会打出 100+ 行/秒
    static int64_t pLastNoLogMs = 0;
    if (idle > 100 && pNow - pLastNoLogMs >= 500) {
      pLastNoLogMs = pNow;
      log(LogLevel::info, "[pace] nodata gap:", (int32_t)idle, " q:",
          (int32_t)frameQueue.size(), " mclk:", mClock);
    }
    pNo++;
    if (idle > pMaxNoData) pMaxNoData = idle;
    nextDueMs = timeStampMS() + 5;  // 没帧: 短轮回, 解码器补上就立即接
    return SyncResult::nodata;
  }
  pLastFrameMs = pNow;
  double speed = clock->getSpeed();
  bool bIFrameMode = false;
  if (mediaPlayer) {
    bIFrameMode = mediaPlayer->getIFrameMode();
  }
  int64_t pts = frame->pts;
  int64_t tPts = prePts;
  // 记录自身基准，frameTimer
  renderFirst();
  // frame没有使用,故不更新,保持正常的duration
  if (pts != prePts) {
    // 更新pts与duration
    updatePts(pts);
  }
  int64_t sduration = duration;
  // 需要调整duration,让简隔时间根据speed变化
  sduration = (int64_t)((double)duration / speed);
  int64_t now = timeStampMS();
  // 时间戳跳变,如何处理？先记录
  // I帧模式下间隔大是正常的,不触发跳变处理
  if (!bIFrameMode && (sduration > AVOX_NOSYNC_THRESHOLD || sduration < 0)) {
    log(LogLevel::warn, "video sync jump:", duration, " now pts:", pts,
        " pre pts:", pts - duration, " main pts:", mClock, " speed:", speed);
    renderTime = now;
    duration = 0;
    sduration = 0;
  }
  // delay<duration视频慢了 delay>duration视频快了
  int64_t delay = computeDelay(sduration);
  // I帧模式不用音频
  if (bIFrameMode) {
    delay = sduration;
  }
  // 还不到渲染时间，让窗口等待
  if (renderTime + delay > now) {
    // 只睡到该帧到期为止的余量: 唤醒粒度造成的早醒在下一拍补齐, 不会丢一整帧
    int64_t wait = renderTime + delay - now;
    if (wait > 100) {
      log(LogLevel::info, "[pace] wait:", (int32_t)wait, " pts:", pts,
          " dur:", (int32_t)duration, " delay:", (int32_t)delay,
          " dif:", (int32_t)syncDiffFiltMs, " sclk:", clock->clock(),
          " mclk:", mClock, " ahead:", (int32_t)(renderTime - now));
    }
    pQuick++;
    if (wait > pMaxWait) pMaxWait = wait;
    nextDueMs = renderTime + delay;
    return SyncResult::quick;
  }
  // 下次更新时间,renderTime应该一直now的时间差不多
  // now受刷新线程影响，如果刷新线程每次比duration慢1-2ms
  // 如果是20FPS,大约20多帧后，renderTime比now小一帧时间，引发丢帧
  renderTime = renderTime + delay;
  // renderTime应该与now差不多
  if (delay > 0 && now - renderTime > AVOX_SYNC_THRESHOLD_MAX) {
    // 高倍率丢帧相对频繁，导致renderTime一直追不上now，不打印
    // log(LogLevel::warn, "reset render time:", renderTime, " now:", now,
    //     " delay:", delay, " duration:", duration, " pts:", pts,
    //     " pre pts:", tPts);
    renderTime = now;
  }
  // 更新时钟并同步给播放器
  updateClock(pts);
// log(LogLevel::info, "main pts:", mClock, " now:", now,
// " render time:", renderTime, " speed:", speed,
  if (!bIFrameMode && now > warmupUntilMs && delay > 0 &&
      now > renderTime + sduration * 1.5 && sduration > 0) {
    int64_t dropPts = 0;
    // GPU数据丢弃需要通知frame好做后续处理
    frameQueue.pop([&](const VideoFramePtr& frame) {
      dropPts = frame->pts;
      frame->release();
    });
    // log(LogLevel::warn, "drop frame pts:", dropPts, " main pts:", mClock,
    //     " now:", now, " render time:", renderTime, " speed:", speed,
    //     " speed duration:", sduration, " pts:", pts, " pre pts:", tPts,
    //     " queue size:", packetQueue.size(),
    //     " frame size:", frameQueue.size());
    // 修正: 丢帧追赶时必须同步推进渲染基准, 否则 delay==0(视频落后主时钟)时
    // renderTime 既不推进、也过不了上方的纠正(纠正条件是 delay>0), 于是 now
    // 越走越远, 该 if 永久成立 —— 每一帧都被丢弃, 画面彻底静止
    // (seek 重置时钟后才短暂恢复)。
    renderTime = now;
    nextDueMs = now + 1;
    pSlow++;
    return SyncResult::slow;
  }
// 视频慢了(delay==0, computeDelay 已把落后钳成 0), 通知窗口快速刷新跳过
// sleep: 若不通知, 唤醒节拍被窗口线程的帧率标签钳住, 落后永远追不回
  if (delay == 0) {
    nextDueMs = timeStampMS() + 1;
    pSlow++;
    return SyncResult::slow;
  }
  // 下一帧的绝对到期时刻(本次含同步微调后的帧距); 1s 上限防坏戳
  nextDueMs = renderTime + std::min<int64_t>(1000, std::max<int64_t>(1, delay));
  pRen++;
  // 交付间隔: 线程/上游被外部卡住时 nodata 与 wait 都不打点, 只有这里留痕
  if (pLastRenMs && pNow - pLastRenMs > 100) {
    log(LogLevel::info, "[pace] deliver gap:", (int32_t)(pNow - pLastRenMs),
        " q:", (int32_t)frameQueue.size(), " mclk:", mClock);
  }
  pLastRenMs = pNow;
  return SyncResult::none;
}

void VideoTrack::pauseRender(bool pause) {
  if (windowRender) {
    windowRender->pause(pause);
  }
  clock->pause(pause);
}

void VideoTrack::pauseDecoder(bool pause) {
  if (decodeTask) {
    if (pause) {
      decodeTask->pauseTask();
    } else {
      decodeTask->resumeTask();
    }
    // 记录解码暂停
    PBMediaAction pb = {};
    pb.mediaObject = MediaObject::decode;
    pb.trackType = trackType;
    pb.action = pause ? MediaAction::pause : MediaAction::resume;
    pushPB<MPPBType::MediaAction>(mpPingback, pb);
  }
}

void VideoTrack::flush() {
  if (decodeTask) {
    decodeTask->flush();
  }
  // 重置队列
  packetQueue.clear();
  frameQueue.clear();
  clock->reset();
  bResetBase = true;
  // 钳位游标随队列复位, seek回跳的第一帧不能被续格前推
  lastInPts = AVOX_NOVALID_PTS;
  // 精确seek丢弃标记随队列复位, 防跨流/失败seek残留
  seekDiscardPts = AVOX_NOVALID_PTS;
  // 高差滤波器随 seek 重锚, 不把旧位姿的偏差带进新位置
  syncDiffInit = false;
  // 记录flush
  PBMediaAction pb = {};
  pb.mediaObject = MediaObject::track;
  pb.trackType = TrackType::video;
  pb.action = MediaAction::flush;
  pushPB<MPPBType::MediaAction>(mpPingback, pb);
}

void VideoTrack::flushFrames() {
  // 只丢已解帧(硬解重置后引用旧GPU上下文), 包队列是有限源的全部存货,
  // IO 已读完全部包(EOF)后清掉无法补充, 解码线程会静默饿死
  frameQueue.clear();
}

void VideoTrack::updateSeekTime(int64_t seekTime) { clock->update(seekTime); }

void VideoTrack::attachSubtitle(SubtitleView* view) {
  subtitleView = view;
  if (!view) {
    return;
  }
  view->setWindowRender(windowRender.get());
  if (srcDesc.width > 0 && srcDesc.height > 0) {
    view->setStorageSize(srcDesc.width, srcDesc.height);
  }
  // 轨通道惰性建立(无插件降级为不渲染字幕轨); 文本路径不经过这里
  view->openTrackChannel();
}

void VideoTrack::close() {
  // 先摘字幕视图(渲染对象即将销毁)
  if (subtitleView) {
    subtitleView->setWindowRender(nullptr);
  }
  packetQueue.setClose(true);
  // 先通知frameQueue不可用，保证decodeTask顺利关闭
  frameQueue.setClose(true);
// 调整下，android下，先关闭EglVideoRender，再关闭AndVDecoder
// 否则可能EglVideoRender可能还在渲染纹理frame->release(true)时
  if (windowRender) {
    windowRender->stop();
    windowRender->setFrameSource(nullptr);
    // 记录渲染关闭
    PBMediaAction pb = {};
    pb.mediaObject = MediaObject::render;
    pb.trackType = trackType;
    pb.action = MediaAction::close;
    pushPB<MPPBType::MediaAction>(mpPingback, pb);
  }
  if (decodeTask) {
    decodeTask->close();
    // 队列里的decoder置空，现在是野指针
    frameQueue.action([&](VideoFramePtr& frame) {
      if (frame->buffer) {
        frame->buffer->reset();
      }
    });
    // 记录解码器关闭
    PBMediaAction pb = {};
    pb.mediaObject = MediaObject::decode;
    pb.trackType = trackType;
    pb.action = MediaAction::close;
    pushPB<MPPBType::MediaAction>(mpPingback, pb);
  }
  // 清空队列
  packetQueue.clear();
  frameQueue.clear();
  // 呈现游标随轨销毁复位: 轨对象跨open复用, 残留游标会把下一轮开头的帧
  // 前向重戳到上一轮停片位, 换片进度从旧停片位起跳/同URL重开钉死在片尾
  lastInPts = AVOX_NOVALID_PTS;
  seekDiscardPts = AVOX_NOVALID_PTS;
  bHdrMetaSent = false;
  onUninitDesc();
}

}
