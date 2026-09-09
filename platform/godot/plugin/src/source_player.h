#pragma once

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/array.hpp>

#include <avox/AvoxPlayer.h>
#include <avox/AvoxSource.h>

#include "render_source.h"

#include <atomic>
#include <memory>

namespace godot {

class SurfaceTextureBridge;

// ISourcePlayer 释放需先 close() (同 voice.h 的 SourcePlayerDeleter 思路;
// 这里独立定义避免与 voice.h 重名冲突)。
struct DevicePlayerDeleter {
    void operator()(avox::ISourcePlayer *p) const {
        if (p) {
            p->close();
            delete p;
        }
    }
};

/// 设备采集播放器 (Node, avox::ISourcePlayer)。摄像头/桌面/窗口/麦克风 → 画面显示 + 可选录制。
/// 复刻 MediaPlayer 的 surface 桥 + IMediaPlayerOb 信号模式。设备经 DeviceManager 枚举后按 id/index 设入。
/// 对应 swig/python/avox/player.py (ISourcePlayer) + source.py。
///
/// 用法:
///   var sp = SourcePlayer.new()
///   add_child(sp)
///   sp.set_video_source_index(0)   # 摄像头 0 (DeviceManager.list_video_devices() 查)
///   sp.open()
///   $TextureRect.texture = sp.get_texture()
class SourcePlayer : public Node, public IRenderSource {
    GDCLASS(SourcePlayer, Node)

public:
    SourcePlayer();
    ~SourcePlayer();

    // 设备源 (open 前设置); 返回是否成功找到并设入。video_id/audio_id 来自 DeviceManager
    bool set_video_source(const String &p_id);
    bool set_video_source_index(int64_t p_index);
    bool set_audio_source(const String &p_id);
    bool set_audio_source_index(int64_t p_index);

    void open();
    void close();

    int get_state() const;             // PlayerState (none=0 opening=1 ready=2 playing=3 ...)
    Dictionary get_source_info() const;
    Ref<Texture2D> get_texture() const;
    bool is_open() const;

    // IRenderSource: 暴露底层 surface render 供 VideoFaceNode 等 tap(未 open 时为 null)。
    avox::ISurfaceRender* getSurfaceRenderRaw() override { return player ? player->getSurfaceRender() : nullptr; }

protected:
    static void _bind_methods();
    void _notification(int p_what);

private:
    friend class SourcePlayerOb;
    std::unique_ptr<avox::ISourcePlayer, DevicePlayerDeleter> player;
    std::unique_ptr<avox::IMediaPlayerOb> playerOb;
    SurfaceTextureBridge *surfaceBridge = nullptr;

    std::atomic<int> stateCache{0};
    std::atomic<bool> sourceReady{false};
    std::atomic<int> videoW{0};
    std::atomic<int> videoH{0};
    // 源色彩空间编码 (encodeColorSpace, -1=未知)。CPU 回退路径 shader 的解码矩阵
    std::atomic<int> colorSpaceCode{-1};

    void createPlayer();
    void destroyPlayer();
    void processFrame();
    void applySourceInfo();
};

/// 设备枚举 (RefCounted, 静态方法)。封装 avox::getVideoManager/getAudioManager (平台默认 SDK)。
/// 对应 swig/python/avox/source.py。
///
/// 用法:
///   for d in DeviceManager.list_video_devices():
///       print(d.name, d.kind_str, d.id)
///   # Windows: index 0..n-1; 0 起为 monitor(桌面)/window, 之后 window
///   # 音频: index 0=默认麦克风, 1=默认声卡回环(loopback), 2+=其余麦克风
class DeviceManager : public RefCounted {
    GDCLASS(DeviceManager, RefCounted)

public:
    DeviceManager();
    ~DeviceManager();

    // 视频设备列表 → [{name,id,kind(0none1camera2window3monitor),kind_str,index}, ...]
    static Array list_video_devices();
    // 音频设备列表 → [{name,id,kind(0none1mic2loopback),kind_str,index}, ...]
    static Array list_audio_devices();
    static void refresh_video_devices();
    static void refresh_audio_devices();

    // C++ 内部: 按 id/index 取裸设备 (SourcePlayer 用, 不绑定 GDScript)
    static avox::IVideoSource *find_video(const String &p_id);
    static avox::IVideoSource *video_at(int64_t p_index);
    static avox::IAudioSource *find_audio(const String &p_id);
    static avox::IAudioSource *audio_at(int64_t p_index);

protected:
    static void _bind_methods();
};

} // namespace godot
