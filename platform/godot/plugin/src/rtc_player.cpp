#include "rtc_player.h"
#include "surface.h"
#include "gpu_passthrough.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

namespace godot {

// ── avox 观察者: 只继承 IRtcPlayerOb (它本身派生自 IMediaPlayerOb, 状态回调一并携带;
//    同时再继承 IMediaPlayerOb 会成菱形继承 → 转 IMediaPlayerOb* 二义) ──
// addRtcPlayerOb 对 IRtcPlayerOb 实例自动双注册, 一次注册状态/rtc 两路回调都收
// avox 线程回调 → call_deferred 转主线程发信号 (同 MediaPlayer 模式)
class RtcPlayerOb : public avox::IRtcPlayerOb {
public:
    RtcPlayer *owner = nullptr;

    void onStateChange(avox::PlayerState preState, avox::PlayerState state) override {
        (void)preState;
        if (owner) {
            owner->stateCache.store(static_cast<int>(state));
            owner->call_deferred("emit_signal", "state_changed", static_cast<int>(state));
        }
    }
    void onReady() override {
        if (owner && owner->player) {
            // onReady 时远端 sourceInfo 一定有效, 取视频尺寸缓存 (主线程消费喂 GPU 直通)
            avox::ISourceInfo *info = owner->player->getRemoteSourceInfo();
            if (info && info->videoSize() > 0) {
                avox::VTrackDesc vd = info->getVideoDesc(0);
                owner->videoW.store(vd.desc.width);
                owner->videoH.store(vd.desc.height);
            }
            owner->sourceReady.store(true);
            owner->call_deferred("emit_signal", "rtc_ready");
        }
    }
    void onIoError(avox::AVError error, const char *msg) override {
        if (owner) {
            owner->call_deferred("emit_signal", "rtc_error", static_cast<int>(error),
                                 String(msg ? msg : ""));
        }
    }
    void onDecodeError(avox::TrackType trackType, avox::DecodeResult error) override {
        (void)trackType;
        if (owner) {
            owner->call_deferred("emit_signal", "rtc_error", static_cast<int>(error),
                                 String(avox::getDecodeResultStr(error)));
        }
    }
    void onConnectionState(avox::RtcConnState state) override {
        if (owner) {
            owner->call_deferred("emit_signal", "connection_state_changed", static_cast<int>(state));
        }
    }
    void onFirstVideoFrame() override {
        if (owner) {
            owner->call_deferred("emit_signal", "first_video_frame");
        }
    }
    void onDataChannelMsg(const char *data, int32_t size) override {
        if (owner) {
            PackedByteArray bytes;
            if (data && size > 0) {
                bytes.resize(size);
                memcpy(bytes.ptrw(), data, static_cast<size_t>(size));
            }
            owner->call_deferred("emit_signal", "data_channel_msg", bytes);
        }
    }
};

// ── 自定义信令转发观察者: 本地 SDP/ICE → 信号 (信令线程, deferred) ──
class RtcSdpAgent : public avox::ISdpAgentOb {
public:
    RtcPlayer *owner = nullptr;

    void onLocalSdp(const char *localSdp) override {
        if (owner) {
            owner->call_deferred("emit_signal", "local_sdp", String(localSdp ? localSdp : ""));
        }
    }
    void onIceCandidate(const char *candidate, const char *mid, int mlineIndex) override {
        if (owner) {
            owner->call_deferred("emit_signal", "ice_candidate",
                                 String(candidate ? candidate : ""), String(mid ? mid : ""), mlineIndex);
        }
    }
};

// ── RtcPlayer ──

RtcPlayer::RtcPlayer() {
    surfaceBridge = new SurfaceTextureBridge();
}

RtcPlayer::~RtcPlayer() {
    destroyPlayer();
    delete surfaceBridge;
}

void RtcPlayer::_notification(int p_what) {
    switch (p_what) {
    case NOTIFICATION_READY:
        set_process(true);
        break;
    case NOTIFICATION_PROCESS:
        processFrame();
        break;
    case NOTIFICATION_EXIT_TREE:
        close_rtc();
        break;
    }
}

void RtcPlayer::createPlayer() {
    if (player) return;
    // 主线程: Godot 已完全初始化, 延迟初始化 GPU 直通 (同 MediaPlayer)
    avox_gpu_passthrough_init();
    player = avox::createWebRtcPlayer();
    if (!player) {
        // 插件未部署: avox 运行期从 <avox.dll目录>/plugins/ 扫描 avox_webrtc
        ERR_PRINT("createWebRtcPlayer 失败: 请确认部署了 plugins/ 目录下的 avox_webrtc 动态插件");
        return;
    }
    playerOb = new RtcPlayerOb();
    static_cast<RtcPlayerOb *>(playerOb)->owner = this;
    avox::addRtcPlayerOb(player, playerOb);
    // 配置 (须在 open 前)
    player->setRollType(static_cast<avox::RtcRollType>(rollType));
    player->setVideoDirection(static_cast<avox::RtpDirection>(videoDir));
    player->setAudioDirection(static_cast<avox::RtpDirection>(audioDir));
    player->setEnableDataChannel(false);
    player->setAutoReconnect(true, 3);
    // 远端画面纹理桥: 绑 remote 渲染器 (与 MediaPlayer 的 surface 同接口)
    surfaceBridge->setGpuPassthroughEnabled(gpuPassthrough);
    surfaceBridge->bindSurface(player->getRemoteSurfaceRender());
    if (volume < 1.0f) {
        if (auto *audio = player->getRemoteAudioRender()) audio->setVolume(volume);
    }
}

void RtcPlayer::destroyPlayer() {
    if (!player) return;
    // 先解绑纹理桥接, 再关播放器 (解绑需要 surface render 仍有效)
    surfaceBridge->unbindSurface();
    // 信令 agent: 解注册后删除 (TestSdpOb 的虚析构在 avox.dll 内, 跨模块 delete 安全)
    if (httpAgent) {
        player->setSdpAgentOb(nullptr);
        delete httpAgent;
        httpAgent = nullptr;
    }
    if (sdpForward) {
        player->setSdpAgentOb(nullptr);
        delete sdpForward;
        sdpForward = nullptr;
    }
    player->close();
    avox::removeRtcPlayerOb(player, playerOb);
    delete playerOb;
    playerOb = nullptr;
    // createWebRtcPlayer() 是裸 new 且无 destroy API, delete 是唯一释放路径 (基类析构 virtual)
    delete player;
    player = nullptr;
    stateCache.store(0);
    sourceReady.store(false);
    videoW.store(0);
    videoH.store(0);
}

void RtcPlayer::connect_signaling(const String &p_url) {
    if (p_url.is_empty()) {
        ERR_PRINT("connect_signaling: 信令地址为空 (自定义信令请用 open_rtc)");
        return;
    }
    signalingUrl = p_url;
    destroyPlayer();
    createPlayer();
    if (!player) return;
    // HTTP 信令 agent (Offer 拉流): onLocalSdp 自动 POST, 远端 answer 自动回填
    httpAgent = avox::createZlTestSdpAgent(player, signalingUrl.utf8().get_data());
    if (!httpAgent) {
        ERR_PRINT("createZlTestSdpAgent 失败 (HTTP 信令 agent 创建失败)");
        return;
    }
    player->setSdpAgentOb(httpAgent);
    player->open();
}

void RtcPlayer::open_rtc() {
    destroyPlayer();
    createPlayer();
    if (!player) return;
    // 自定义信令: local_sdp/ice_candidate 信号发出, 远端消息用 set_remote_sdp 回填
    sdpForward = new RtcSdpAgent();
    static_cast<RtcSdpAgent *>(sdpForward)->owner = this;
    player->setSdpAgentOb(sdpForward);
    player->open();
}

void RtcPlayer::close_rtc() {
    destroyPlayer();
}

void RtcPlayer::reconnect_rtc() {
    if (!player) {
        if (!signalingUrl.is_empty()) {
            connect_signaling(signalingUrl);
        } else {
            open_rtc();
        }
        return;
    }
    // 已有实例: avox 内部重建 PeerConnection 并重新走信令
    player->reconnect();
}

void RtcPlayer::set_signaling_url(const String &p_url) {
    signalingUrl = p_url;
}

String RtcPlayer::get_signaling_url() const {
    return signalingUrl;
}

void RtcPlayer::set_roll_type(int p_type) {
    rollType = p_type;
    if (player) player->setRollType(static_cast<avox::RtcRollType>(p_type));
}

int RtcPlayer::get_roll_type() const {
    return rollType;
}

void RtcPlayer::set_video_direction(int p_dir) {
    videoDir = p_dir;
    if (player) player->setVideoDirection(static_cast<avox::RtpDirection>(p_dir));
}

int RtcPlayer::get_video_direction() const {
    return videoDir;
}

void RtcPlayer::set_audio_direction(int p_dir) {
    audioDir = p_dir;
    if (player) player->setAudioDirection(static_cast<avox::RtpDirection>(p_dir));
}

int RtcPlayer::get_audio_direction() const {
    return audioDir;
}

void RtcPlayer::add_ice_server(const String &p_url, const String &p_user, const String &p_pwd) {
    if (!player) return;
    player->addIceServer(p_url.utf8().get_data(),
                         p_user.is_empty() ? nullptr : p_user.utf8().get_data(),
                         p_pwd.is_empty() ? nullptr : p_pwd.utf8().get_data());
}

void RtcPlayer::set_auto_reconnect(bool p_enable, int p_retries) {
    if (player) player->setAutoReconnect(p_enable, p_retries);
}

void RtcPlayer::set_send_video_bitrate(int p_kbps) {
    if (player) player->setSendVideoBitrate(p_kbps);
}

void RtcPlayer::set_send_video_fps(int p_fps) {
    if (player) player->setSendVideoFps(p_fps);
}

void RtcPlayer::set_preferred_video_codec(const String &p_codec) {
    if (player) player->setPreferredVideoCodec(p_codec.utf8().get_data());
}

void RtcPlayer::set_enable_data_channel(bool p_enable) {
    if (player) player->setEnableDataChannel(p_enable);
}

void RtcPlayer::set_gpu_passthrough(bool p_enable) {
    gpuPassthrough = p_enable;
    if (surfaceBridge) {
        surfaceBridge->setGpuPassthroughEnabled(p_enable);
    }
}

bool RtcPlayer::get_gpu_passthrough() const {
    return gpuPassthrough;
}

void RtcPlayer::set_volume(float p_volume) {
    volume = p_volume;
    if (player) {
        if (auto *audio = player->getRemoteAudioRender()) audio->setVolume(p_volume);
    }
}

float RtcPlayer::get_volume() const {
    return volume;
}

void RtcPlayer::set_remote_sdp(const String &p_sdp) {
    if (!player || p_sdp.is_empty()) return;
    player->setRemoteSdp(p_sdp.utf8().get_data());
}

String RtcPlayer::get_local_sdp() const {
    if (!player) return String();
    const char *sdp = player->getLocalSdp();
    return String(sdp ? sdp : "");
}

void RtcPlayer::add_ice_candidate(const String &p_candidate, const String &p_mid, int p_mline) {
    if (!player || p_candidate.is_empty()) return;
    player->addIceCandidate(p_candidate.utf8().get_data(), p_mid.utf8().get_data(), p_mline);
}

bool RtcPlayer::send_data_channel(const PackedByteArray &p_data) {
    if (!player || p_data.size() <= 0) return false;
    return player->sendDataChannel((const char *)p_data.ptr(), p_data.size());
}

int RtcPlayer::get_state() const {
    return stateCache.load();
}

int RtcPlayer::get_connection_state() const {
    return player ? static_cast<int>(player->getConnectionState()) : static_cast<int>(avox::RtcConnState::closed);
}

double RtcPlayer::get_fps() const {
    return player ? player->getFps() : 0.0;
}

float RtcPlayer::get_loss_rate() const {
    return player ? player->getLossRate() : 0.0f;
}

int RtcPlayer::get_rtt_ms() const {
    return player ? player->getRttMs() : -1;
}

Ref<Texture2D> RtcPlayer::get_texture() const {
    if (surfaceBridge) {
        return surfaceBridge->getTexture();
    }
    return Ref<Texture2D>();
}

void RtcPlayer::processFrame() {
    // onReady 后首次把远端视频真实尺寸喂给 GPU 直通路径 (同 MediaPlayer)
    if (sourceReady.load() && surfaceBridge) {
        int w = videoW.load();
        int h = videoH.load();
        if (w > 0 && h > 0) {
            surfaceBridge->setVideoSize(w, h);
        }
    }
    if (surfaceBridge) {
        surfaceBridge->update();
    }
}

void RtcPlayer::_bind_methods() {
    // 连接控制
    ClassDB::bind_method(D_METHOD("connect_signaling", "url"), &RtcPlayer::connect_signaling);
    ClassDB::bind_method(D_METHOD("open_rtc"), &RtcPlayer::open_rtc);
    ClassDB::bind_method(D_METHOD("close_rtc"), &RtcPlayer::close_rtc);
    ClassDB::bind_method(D_METHOD("reconnect_rtc"), &RtcPlayer::reconnect_rtc);

    // 配置
    ClassDB::bind_method(D_METHOD("set_signaling_url", "url"), &RtcPlayer::set_signaling_url);
    ClassDB::bind_method(D_METHOD("get_signaling_url"), &RtcPlayer::get_signaling_url);
    ClassDB::bind_method(D_METHOD("set_roll_type", "type"), &RtcPlayer::set_roll_type);
    ClassDB::bind_method(D_METHOD("get_roll_type"), &RtcPlayer::get_roll_type);
    ClassDB::bind_method(D_METHOD("set_video_direction", "dir"), &RtcPlayer::set_video_direction);
    ClassDB::bind_method(D_METHOD("get_video_direction"), &RtcPlayer::get_video_direction);
    ClassDB::bind_method(D_METHOD("set_audio_direction", "dir"), &RtcPlayer::set_audio_direction);
    ClassDB::bind_method(D_METHOD("get_audio_direction"), &RtcPlayer::get_audio_direction);
    ClassDB::bind_method(D_METHOD("add_ice_server", "url", "user", "pwd"), &RtcPlayer::add_ice_server, DEFVAL(""), DEFVAL(""));
    ClassDB::bind_method(D_METHOD("set_auto_reconnect", "enable", "retries"), &RtcPlayer::set_auto_reconnect, DEFVAL(true), DEFVAL(3));
    ClassDB::bind_method(D_METHOD("set_send_video_bitrate", "kbps"), &RtcPlayer::set_send_video_bitrate);
    ClassDB::bind_method(D_METHOD("set_send_video_fps", "fps"), &RtcPlayer::set_send_video_fps);
    ClassDB::bind_method(D_METHOD("set_preferred_video_codec", "codec"), &RtcPlayer::set_preferred_video_codec);
    ClassDB::bind_method(D_METHOD("set_enable_data_channel", "enable"), &RtcPlayer::set_enable_data_channel);
    ClassDB::bind_method(D_METHOD("set_gpu_passthrough", "enable"), &RtcPlayer::set_gpu_passthrough);
    ClassDB::bind_method(D_METHOD("get_gpu_passthrough"), &RtcPlayer::get_gpu_passthrough);
    ClassDB::bind_method(D_METHOD("set_volume", "volume"), &RtcPlayer::set_volume);
    ClassDB::bind_method(D_METHOD("get_volume"), &RtcPlayer::get_volume);

    // 信令回填 / DataChannel
    ClassDB::bind_method(D_METHOD("set_remote_sdp", "sdp"), &RtcPlayer::set_remote_sdp);
    ClassDB::bind_method(D_METHOD("get_local_sdp"), &RtcPlayer::get_local_sdp);
    ClassDB::bind_method(D_METHOD("add_ice_candidate", "candidate", "mid", "mline"), &RtcPlayer::add_ice_candidate);
    ClassDB::bind_method(D_METHOD("send_data_channel", "data"), &RtcPlayer::send_data_channel);

    // 查询
    ClassDB::bind_method(D_METHOD("get_state"), &RtcPlayer::get_state);
    ClassDB::bind_method(D_METHOD("get_connection_state"), &RtcPlayer::get_connection_state);
    ClassDB::bind_method(D_METHOD("get_fps"), &RtcPlayer::get_fps);
    ClassDB::bind_method(D_METHOD("get_loss_rate"), &RtcPlayer::get_loss_rate);
    ClassDB::bind_method(D_METHOD("get_rtt_ms"), &RtcPlayer::get_rtt_ms);
    ClassDB::bind_method(D_METHOD("get_texture"), &RtcPlayer::get_texture);

    // 属性
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "signaling_url"), "set_signaling_url", "get_signaling_url");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "roll_type"), "set_roll_type", "get_roll_type");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "video_direction"), "set_video_direction", "get_video_direction");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "audio_direction"), "set_audio_direction", "get_audio_direction");
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "gpu_passthrough"), "set_gpu_passthrough", "get_gpu_passthrough");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "volume", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_volume", "get_volume");

    // 信号 ("ready" 是 Node 内置信号, 用 rtc_ready 避让)
    ADD_SIGNAL(MethodInfo("state_changed", PropertyInfo(Variant::INT, "state")));
    ADD_SIGNAL(MethodInfo("connection_state_changed", PropertyInfo(Variant::INT, "state")));
    ADD_SIGNAL(MethodInfo("rtc_ready"));
    ADD_SIGNAL(MethodInfo("first_video_frame"));
    ADD_SIGNAL(MethodInfo("rtc_error", PropertyInfo(Variant::INT, "code"), PropertyInfo(Variant::STRING, "msg")));
    ADD_SIGNAL(MethodInfo("local_sdp", PropertyInfo(Variant::STRING, "sdp")));
    ADD_SIGNAL(MethodInfo("ice_candidate", PropertyInfo(Variant::STRING, "candidate"),
                          PropertyInfo(Variant::STRING, "mid"), PropertyInfo(Variant::INT, "mline")));
    ADD_SIGNAL(MethodInfo("data_channel_msg", PropertyInfo(Variant::PACKED_BYTE_ARRAY, "data")));
}

} // namespace godot
