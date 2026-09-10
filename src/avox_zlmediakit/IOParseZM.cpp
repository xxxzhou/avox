#include "IOParseZM.hpp"

#include <functional>
#include <mutex>

#include "ZlmHelper.hpp"
#include "avox/module/AvoxManager.hpp"
#include "avox/module/TaskTrack.hpp"

#ifdef __APPLE__
#include <memory>

#include "Util/logger.h"
#endif

namespace avox {

// ZLMediaKit 日志级别 -> avox LogLevel
// ZL: LTrace=0, LDebug=1, LInfo=2, LWarn=3, LError=4
static LogLevel zmToAvoxLevel(int zmLevel) {
  switch (zmLevel) {
    case 0:  // LTrace
    case 1:  // LDebug
      return LogLevel::debug;
    case 2:  // LInfo
      return LogLevel::info;
    case 3:  // LWarn
      return LogLevel::warn;
    case 4:  // LError
    default:
      return LogLevel::error;
  }
}

static void API_CALL onZmLog(int level, const char* file, int line,
                             const char* function, const char* message) {
  LogLevel avoxLevel = zmToAvoxLevel(level);
  // 带上 ZL 源文件信息，方便排查
  std::string msg;
  string_format(msg, "[ZL] ", extractFileInline(file), ":", line, " ", function,
                " ", message ? message : "");
  logMsg(avoxLevel, msg.c_str());
}

void initZmEnv() {
  // Apple/iOS 静态链下本函数曾由 AvoxManager::init() 在静态初始化阶段执行, 而
  // ZLM/ZLToolKit 的事件名常量(config.cpp/logger.cpp 的 const std::string)的本体
  // 构造函数排在 libavox 之后 → mk_events_listen 拿到全空 key, 22 类监听塌缩到
  // 同一分发器, 静态期首条日志 any_cast 撞类型未捕获直接崩(真机 iOS 实测)。
  // 改为首次创建 ZLM IO/Muxer 时才初始化(call_once 保幂等), 此时已过静态期。
  static std::once_flag s_zmEnvOnce;
  std::call_once(s_zmEnvOnce, []() {
#ifdef __APPLE__
    // Apple 是静态链, 没有 DllMain(DETACH) 这个时机去触发 AvoxManager::clean()
    // 里的 mk_env_release, ZLToolKit 的 Logger 单例会一路撑到静态析构期; 而
    // ~Logger 里还会写日志, LogContextCapture 析构时锁的 mutex 已析构 ->
    // 抛 std::system_error(recursive_mutex lock failed) 无人接 -> abort(EXIT=134),
    // 判定行虽已打完但退出码骗自动化。故意泄漏一份引用让 ~Logger 永不执行
    // (同 RtcEngine 泄漏 PeerConnectionFactory 的策略)。
    static auto* leakedZlLogger =
        new std::shared_ptr<toolkit::Logger>(toolkit::Logger::Instance().shared_from_this());
    (void)leakedZlLogger;
#endif
    // log_mask: 只用 LOG_CALLBACK，不用 LOG_CONSOLE/LOG_FILE
    // log_level: 2 = LInfo，过滤掉 0Trace/1Debug
    mk_env_init2(1, 0, LOG_CALLBACK, nullptr, 0, 0, nullptr, 0, nullptr, nullptr);

    mk_events events = {};
    events.on_mk_log = onZmLog;
    mk_events_listen(&events);
  });
}

void regZmIO() {
  RegFunc regFunc = {"zlmediakit io init", []() {
                       IoPlanDesc zlDesc = {};
                       // 初始化 例如名称、是否支持硬件加速等
                       zlDesc.name = "zlmediakit";
                       AvoxManager::Get().ioSources.regInitFunc(
                           IoPlan::zlmediakit, zlDesc,
                           []() -> AVSource* { return new IOParseZM(); });
                     }};
  AvoxManager::Get().initFuncs.push_back(regFunc);
  RegFunc cleanFunc = {"zlmediakit io clean", []() {
                         // 停 server + 异步日志线程并摘除 EventChannel(见
                         // mk_common.cpp mk_env_release 注释); 必须在静态析构前
                         // 调, 否则 Logger/NoticeCenter 析构序倒挂会在退出时崩
                         mk_env_release();
                         // 清空事件回调(含 on_mk_log), 退出时 ZL 不再回调到 avox
                         // 已析构对象
                         mk_events_listen(nullptr);
                       }};
  AvoxManager::Get().cleanFuncs.push_back(cleanFunc);
}

IOParseZM::IOParseZM() {
  // 首个 ZLM IO 实例化时才初始化环境+事件(此时已过静态初始化期)
  initZmEnv();
  // h264_prefix=1: 合并同一AU的帧(SPS+PPS+IDR)，使用AnnexB前缀(00 00 00 01)
  zlMerger = mk_frame_merger_create(1);
  // 记录创建线程（player 主线程），ZM socket 线程据此归属到所属 TaskTrack
  parentTid = std::this_thread::get_id();
}

IOParseZM::~IOParseZM() { close(); }

void IOParseZM::onResume() { dispatch(&IAVSourceOb::onReopen); }

void IOParseZM::sampleStats() {
  // 在 poller 线程执行(onPacket 回调)。ZLMediaKit
  // 内部状态(_rtcp_context/_demuxer) 仅在 poller 线程访问安全, 这里采样后
  // player 线程的 getter 只读缓存, 不再跨线程裸读。 否则 mk_player_loss_rate
  // 内部拷 _rtcp_context 的 shared_ptr 时, 会与 poller 线程的 track
  // reset/teardown 并发, 拷到悬空控制块(lock inc [rcx+8])而崩。
  if (!zlPlayer) {
    return;
  }
  // 都是读计数器/拷一个 shared_ptr, 很轻; 每帧采, compateIoTime 直接拿最新值
  cProgress = mk_player_progress(zlPlayer);
  cPosition = (int64_t)mk_player_progress_pos(zlPlayer) * 1000;
  cLossRateVideo = mk_player_loss_rate(zlPlayer, 0);
  cLossRateAudio = mk_player_loss_rate(zlPlayer, 1);
}

bool IOParseZM::onPacket(mk_frame frame) {
  // close后立刻早退: merger可能已被onClose释放, 再入merger是UAF(mk_api崩溃)
  if (bStop) {
    return true;
  }
  sampleStats();
  // ZM socket 线程：归属到创建本 IOParseZM 的 player（bindTid 内部去重）
  // TrackMgr::get().bindonZmLogTid(parentTid, std::this_thread::get_id());
  AvoxPacket packet = zlAvoxPacket(frame);
  PackType packType = (PackType)packet.packtype;
  bool bVideo = packType == PackType::video || packType == PackType::vconfig;
  bool bAudio = packType == PackType::audio || packType == PackType::aconfig;
  if (bDisableAudio && bAudio) {
    return true;
  }
  if (bDisableVideo && bVideo) {
    return true;
  }
  // 第一个音频包也做配置包
  if (bFirstAudio && zlPacketType(frame) == PackType::audio) {
    packet.packtype = (int32_t)PackType::aconfig;
    dispatch(&IAVSourceOb::onPacket, packet);
    bFirstAudio = false;
  }
  // 视频帧使用merger合并同一AU的帧，防止一个I帧被拆成多个包
  if (bVideo && zlMerger) {
    mk_frame_merger_input(
        zlMerger, frame,
        [](void* user_data, uint64_t dts, uint64_t pts, mk_buffer buffer,
           int have_key_frame) {
          auto self = (IOParseZM*)user_data;
          if (!self || !buffer) {
            return;
          }
          AvoxPacket mergedPacket = {};
          mergedPacket.packtype = (int32_t)PackType::video;
          mergedPacket.index = 0;
          mergedPacket.pts = (int64_t)pts;
          mergedPacket.dts = (int64_t)dts;
          mergedPacket.frameType = have_key_frame ? 1 : 0;
          mergedPacket.prefixSize = 4;
          mergedPacket.data.bRef = false;
          mergedPacket.data.size = (int32_t)mk_buffer_get_size(buffer);
          mergedPacket.data.data = (uint8_t*)mk_buffer_get_data(buffer);
          self->rewriteSeekPts(mergedPacket.pts, mergedPacket.dts,
                               PackType::video);
          self->processPacket(mergedPacket);
        },
        this);
  } else {
    rewriteSeekPts(packet.pts, packet.dts, packType);
    processPacket(packet);
  }
  return true;
}

void IOParseZM::rewriteSeekPts(int64_t& pts, int64_t& dts, PackType packtype) {
  if (seekAbsTarget < 0) {
    return;  // 没 seek 过(本地/直播/初次), 不重构
  }
  int track =
      (packtype == PackType::video || packtype == PackType::vconfig) ? 0 : 1;
  // seek 后该 track 第一个包: 锚到 seek 目标绝对 PTS
  if (seekPtsPending[track]) {
    seekPtsOffset[track] = seekAbsTarget - pts;
    seekPtsPending[track] = false;
  }
  pts += seekPtsOffset[track];
  dts += seekPtsOffset[track];
}

bool IOParseZM::onOpen() {
  zlPlayer = mk_player_create();
  bFirstAudio = true;
  bStop = false;
  cduration = 0;
  // 重新创建merger（onClose时释放了）
  if (!zlMerger) {
    zlMerger = mk_frame_merger_create(1);
  }
  mk_frame_merger_clear(zlMerger);
  // 设置一些选项
  // mk_set_option("general.addMuteAudio", "1");
  // wait_add_track_ms: 只有1个Track时等第二个Track来, 用trackReadyMs(默认3s)
  // wait_track_ready_ms: Track有了但未就绪(等配置帧), 用timeoutMs(默认8s,
  // 首帧可能较慢)
  mk_set_option("general.wait_track_ready_ms",
                std::to_string(timeoutMs).c_str());
  mk_set_option("general.wait_add_track_ms",
                std::to_string(trackReadyMs).c_str());
  mk_set_option("general.unready_frame_cache", "1000");
  // RTP接收超时, 默认5秒太短, 国标设备首帧可能较慢
  mk_player_set_option(zlPlayer, "media_timeout_ms",
                       std::to_string(timeoutMs).c_str());
  mk_player_set_option(zlPlayer, "protocol_timeout_ms",
                       std::to_string(timeoutMs).c_str());
  // RTP传输方式: 0=TCP, 1=UDP, 2=MULTICAST
  mk_player_set_option(zlPlayer, "rtp_type",
                       rtspTransport == "udp" ? "1" : "0");
  // 只拉视频track，避免某些IPC音视频共享SSRC/interleaved导致seq不连续
  // mk_player_set_option(zlPlayer, "play_track", "1");
  mk_player_set_on_result(
      zlPlayer,
      [](void* user_data, int err_code, const char* err_msg, mk_track tracks[],
         int track_count) {
        auto self = (IOParseZM*)user_data;
        if (!self) {
          return;
        }
        // 保存 poller 线程（此回调在 poller 线程上执行），用于 onClose 同步释放
        self->zlPoller = mk_thread_from_pool();
        // ZM socket 线程归属到所属 player
        TrackMgr::get().bindTid(self->parentTid, std::this_thread::get_id());
        if (err_code) {
          LOGFLF(LogLevel::warn,
                 "zlmediakit on_result error: err_code=", err_code,
                 " err_msg=", err_msg ? err_msg : "null");
          self->dispatch(&IAVSourceOb::onError, zlIoError(err_code), err_msg);
          return;
        }
        self->videoTracks.clear();
        self->audioTracks.clear();
        for (int i = 0; i != track_count; ++i) {
          if (mk_track_is_video(tracks[i])) {
            if (self->bDisableVideo) {
              continue;
            }
            VTrackDesc vdesc = {};
            vdesc.codecId = zlVCodec(mk_track_codec_id(tracks[i]));
            vdesc.trackId = 0;
            vdesc.desc.width = mk_track_video_width(tracks[i]);
            vdesc.desc.height = mk_track_video_height(tracks[i]);
            vdesc.desc.fps = mk_track_video_fps(tracks[i]);
            self->addVideoDesc(vdesc);
          } else {
            if (self->bDisableAudio) {
              continue;
            }
            ATrackDesc adesc = {};
            adesc.codecId = zlACodec(mk_track_codec_id(tracks[i]));
            adesc.trackId = 0;
            adesc.desc.sampleRate = mk_track_audio_sample_rate(tracks[i]);
            adesc.desc.channels = mk_track_audio_channel(tracks[i]);
            adesc.desc.format =
                zlAudioFromat(mk_track_audio_sample_bit(tracks[i]));
            self->addAudioDesc(adesc);
          }
          void* delegateTag = mk_track_add_delegate(
              tracks[i],
              [](void* user_data, mk_frame frame) {
                auto self = (IOParseZM*)user_data;
                if (self) {
                  self->onPacket(frame);
                }
              },
              self);
        }
        // mk_player_duration从SDP的a=range获取总时长(秒),比mk_track_duration帧累计更准
        float durSec = mk_player_duration(self->zlPlayer);
        if (durSec > 0) {
          self->cduration = (int64_t)(durSec * 1000);
        }
        LOGFLF(LogLevel::info, "io duration:", self->cduration, "ms");
        if (durSec <= 0) {
          self->sourceMode = AVSourceMode::live;
        } else {
          self->sourceMode = AVSourceMode::downLive;
        }
        self->trackReady();
      },
      this);
  mk_player_set_on_shutdown(
      zlPlayer,
      [](void* user_data, int err_code, const char* err_msg, mk_track tracks[],
         int track_count) {
        auto self = (IOParseZM*)user_data;
        if (!self) {
          return;
        }
        LOGFLF(LogLevel::info, "zlmediakit shutdown: err_code=", err_code,
               " err_msg=", err_msg ? err_msg : "null");
        if (err_code) {
          self->dispatch(&IAVSourceOb::onError, zlIoError(err_code), err_msg);
        } else {
          self->dispatch(&IAVSourceOb::onComplete);
        }
        // 解除归属关系
        TrackMgr::get().unbindTid(std::this_thread::get_id());
      },
      this);
  if (speed != 1.0) {
    // play前配置,ZLMediaKit在SETUP完成后直接发带Scale的PLAY(仅点播/NVR回放源有效)
    mk_player_set_option(zlPlayer, "rtsp_speed",
                         std::to_string(speed).c_str());
  }
  // 打开播放
  mk_player_play(zlPlayer, url.c_str());
  return true;
}

void IOParseZM::onClose() {
  bStop = true;
  if (zlPlayer) {
    if (zlPoller) {
      mk_player player = zlPlayer;
      mk_sync_do(
          zlPoller,
          [](void* user_data) {
            auto p = (mk_player)user_data;
            // 兜底再摘一次(理论上主线程已摘), 然后释放
            mk_player_set_on_result(p, nullptr, nullptr);
            mk_player_set_on_shutdown(p, nullptr, nullptr);
            mk_player_release(p);
          },
          player);
      zlPoller = nullptr;
    } else {
      mk_player_set_on_result(zlPlayer, nullptr, nullptr);
      mk_player_set_on_shutdown(zlPlayer, nullptr, nullptr);
      mk_player_release(zlPlayer);
    }
    zlPlayer = nullptr;
  }
  // merger必须在player release(帧回调全部停止)之后释放: 若先释放,
  // ZLM线程可能正拿着merger在onPacket/mk_frame_merger_input里,
  // 访问已释放内存 = mk_api.dll 0xC0000005 (转码/转封装stop时偶现)
  if (zlMerger) {
    mk_frame_merger_release(zlMerger);
    zlMerger = nullptr;
  }
}

void IOParseZM::pause(bool bFlag) {
  if (!zlPlayer) {
    return;
  }
  mk_player_pause(zlPlayer, bFlag);
  LOGFLF(LogLevel::info, "pause:", bFlag);
}

SeekType IOParseZM::seekType() const {
  // cduration > 0 表示是VOD点播流，可以seek
  if (cduration > 0) {
    return SeekType::time;
  }
  // VOD点播模式: 让NtpStamp接受seek后的RTP跳变
  // 有些RTSP流SDP带range自动设置,有些国标转RTSP不带range需手动指定
  return SeekType::time;
}

void IOParseZM::seekTo(double progress) {
  if (!zlPlayer) {
    return;
  }
  mk_player_seekto(zlPlayer, progress);
}

bool IOParseZM::seekTo(int64_t pos) {
  if (!zlPlayer) {
    return false;
  }
  // pos是绝对PTS(墙上时钟ms);npt = (pos - baseTimeMS) / 1000
  int64_t targetMs = pos - baseTimeMS;
  if (targetMs < 0) {
    targetMs = 0;
  }
  int32_t targetSec = (int32_t)(targetMs / 1000);
  LOGFLF(LogLevel::info, "[seekTo] pos=", pos, " baseTimeMS=", baseTimeMS,
         " targetMs=", targetMs, " targetSec=", targetSec);
  // 标记 seek: 后续包 pts 重构锚定到 pos(真实目标绝对PTS)
  seekAbsTarget = pos;
  seekPtsPending[0] = seekPtsPending[1] = true;
  // seek保护期: 第一个video I帧到达前不做重复GOP检测, 避免seek后I帧被误判为重复
  bSeeking = true;
  mk_player_seekto_pos(zlPlayer, targetSec);
  return true;
}

int64_t IOParseZM::duration() const {
  // cduration 在 on_result 回调(poller线程)采样, 已是缓存; 不再跨线程读
  // mk_player_duration
  return cduration;
}

double IOParseZM::progress() const { return cProgress; }

int64_t IOParseZM::position() const { return cPosition; }

void IOParseZM::onSpeed() {
  if (!zlPlayer) {
    return;
  }
  // std::string speedStr;
  // string_format(speedStr, speed);
  // mk_player_set_option(zlPlayer, "rtsp_speed", speedStr.c_str());
  mk_player_speed(zlPlayer, speed);
  LOGFLF(LogLevel::info, "speed:", speed);
}

float IOParseZM::getLossRate(TrackType type) {
  // 直接读 poller 线程采样的缓存; 不再调 mk_player_loss_rate(其内部读
  // _rtcp_context, 与 poller 线程的 track reset/teardown 并发会拷到悬空
  // shared_ptr 而崩)
  return (type == TrackType::video) ? cLossRateVideo : cLossRateAudio;
}

}
