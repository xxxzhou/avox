#pragma once

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>

#include <avox/AvoxPlayer.h>

#include <atomic>
#include <memory>

namespace godot {
class SurfaceTextureBridge;

/// avox WebRTC 推拉流 (对应 avox IRtcPlayer, 同 UE 的 AvoxRtcPlayerComponent)
///
/// 拉流: roll_type=OFFER + connect_signaling(url) 填 HTTP 信令地址 (ZLM type=play/WHEP),
///       远端画面经 get_texture() 显示
/// 推流: roll_type=ANSWER + 长连接信令收 offer 后 set_remote_sdp; 方向用 video/audio_direction
/// 自定义信令: 不调 connect_signaling, 直接 open_rtc, 监听 local_sdp/ice_candidate 信号
///       自己送出, 收到远端消息后 set_remote_sdp/add_ice_candidate 回填
/// 连接结果看 connection_state_changed (CONNECTED 才是真正连上), first_video_frame 隐藏 loading
class RtcPlayer : public Node {
    GDCLASS(RtcPlayer, Node)

public:
    RtcPlayer();
    ~RtcPlayer();

    // ── 连接控制 ──
    // HTTP 信令一键连接 (创建 ZLM/WHEP 型信令 agent 并 open, Offer 角色主路径)
    void connect_signaling(const String &p_url);
    // 自定义信令模式: 先连 local_sdp/ice_candidate 信号再调
    void open_rtc();
    void close_rtc();
    // 手动重连: 重建 PeerConnection 并重新走信令
    void reconnect_rtc();

    // ── 配置 (open 前设置生效) ──
    void set_signaling_url(const String &p_url);
    String get_signaling_url() const;
    // 0=offer(主动发起) 1=answer(被动接受)
    void set_roll_type(int p_type);
    int get_roll_type() const;
    // 方向: 0=inactive 1=recvonly 2=sendonly 3=sendrecv (默认 3, 只拉流用 1)
    void set_video_direction(int p_dir);
    int get_video_direction() const;
    void set_audio_direction(int p_dir);
    int get_audio_direction() const;
    void add_ice_server(const String &p_url, const String &p_user, const String &p_pwd);
    void set_auto_reconnect(bool p_enable, int p_retries);
    void set_send_video_bitrate(int p_kbps);
    void set_send_video_fps(int p_fps);
    void set_preferred_video_codec(const String &p_codec);
    void set_enable_data_channel(bool p_enable);
    // GPU 直通开关 (默认开)。Vulkan 后端零拷贝, false 走 CPU 回退。须在 open 前设置。
    void set_gpu_passthrough(bool p_enable);
    bool get_gpu_passthrough() const;
    void set_volume(float p_volume);
    float get_volume() const;

    // ── 信令回填 (自定义信令) ──
    // 设置远端 SDP (Offer 方填 answer, Answer 方填 offer)
    void set_remote_sdp(const String &p_sdp);
    // 本地 SDP (open 后生成, 也可等 local_sdp 信号)
    String get_local_sdp() const;
    void add_ice_candidate(const String &p_candidate, const String &p_mid, int p_mline);

    // ── DataChannel ──
    bool send_data_channel(const PackedByteArray &p_data);

    // ── 查询 ──
    int get_state() const;
    int get_connection_state() const;
    double get_fps() const;
    float get_loss_rate() const;
    int get_rtt_ms() const;
    Ref<Texture2D> get_texture() const;

protected:
    static void _bind_methods();
    void _notification(int p_what);

private:
    friend class RtcPlayerOb;
    friend class RtcSdpAgent;

    // avox WebRTC 播放器
    avox::IRtcPlayer *player = nullptr;
    avox::IMediaPlayerOb *playerOb = nullptr;
    // 内置 ZLM/WHEP 信令观察者 (connect_signaling 模式创建)
    avox::IRtcEventOb *sdpAgent = nullptr;
    // 远端画面纹理桥 (绑 getRemoteSurfaceRender, 与 MediaPlayer 同款)
    SurfaceTextureBridge *surfaceBridge = nullptr;

    // 配置缓存 (open 前设置)
    String signalingUrl;
    int rollType = 0;
    int videoDir = 3;
    int audioDir = 3;
    bool gpuPassthrough = true;
    float volume = 1.0f;

    std::atomic<int> stateCache{0};
    std::atomic<bool> sourceReady{false};
    std::atomic<int> videoW{0};
    std::atomic<int> videoH{0};
    // 源色彩空间编码 (encodeColorSpace, -1=未知)。CPU 回退路径 shader 的解码矩阵
    std::atomic<int> colorSpaceCode{-1};

    void createPlayer();
    void destroyPlayer();
    void processFrame();
};
} // namespace godot
