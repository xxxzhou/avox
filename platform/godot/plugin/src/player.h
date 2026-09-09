#pragma once

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/variant/dictionary.hpp>

#include <avox/AvoxPlayer.h>
#include <avox/AvoxLayer.h>
#include <avox/AvoxAudio.h>
#include <avox/AvoxSource.h>
#include <avox/AvoxBase.h>

#include "render_source.h"

#include <memory>
#include <mutex>
#include <queue>

namespace godot {
class MediaPlayer : public Node, public IRenderSource {
    GDCLASS(MediaPlayer, Node)

public:
    MediaPlayer();
    ~MediaPlayer();

    // ── GDScript 可调用的方法 ──
    void play(const String &p_url = "");
    void stop();
    void seek(int64_t p_pos_ms);
    void pause();
    void resume();
    void setSpeed(double p_speed);

    bool checkPlaying() const;
    int getState() const;
    double getDuration() const;
    double getPosition() const;
    double getProgress() const;
    int getFps() const;

    // 媒体/编解码信息(URL/时长/canSeek + 各视频/音频流的 codec/分辨率/采样率等), onReady 后才有完整值
    Dictionary getMediaInfo() const;

    // IO 方案 (1=zlmediakit 2=ffmpeg)。注释: "下次打开启用" —— 立即生效需下次 open。
    void setIoPlan(int p_plan);

    // 暴露 avox 选项 (RefCounted 借用包装)。GDScript 侧用 opt.set("mp.log.source.inpacket", true) 等。
    // 注: avox 播放器对象在首次 play() 才创建, 之前 get_option() 返回空。
    Ref<class AvoxOption> getOption();

    // 预设选项 (任意 avox option 键, 如 "torrent.fileIndex")。
    // 首次 play() 前调用会暂存, createPlayer 时统一写入(此时 open 命令尚未执行,
    // IOParseTorrent 在 onOpen 快照前必能收到); 播放器已存在时立即写入。
    void setOption(const String &p_key, const Variant &p_value);

    // ── 字幕 ──
    // 加载外挂字幕文件(.srt)。每次 play() 重建播放器会清掉字幕, 须在 onReady 后重新加载。
    bool loadSubtitle(const String &p_path);
    // 关闭已加载字幕 (同时关闭 ASR)
    void closeSubtitle();

    // ── 属性 ──
    void setUrl(const String &p_url);
    String getUrl() const;

    void setHardDecode(bool p_enable);
    bool getHardDecode() const;

    // GPU 直通开关 (默认开)。Vulkan 后端 + volk 就绪时零拷贝直通,
    // 置 false 走 CPU 回退 (NV12 回读)。须在 play() 前设置。
    void setGpuPassthrough(bool p_enable);
    bool getGpuPassthrough() const;

    void setVolume(float p_volume);
    float getVolume() const;

    // ── 纹理输出 ──
    Ref<Texture2D> getTexture() const;

    // IRenderSource: 暴露底层 surface render 供 VideoFaceNode 等做 enableImage tap。
    // player 每次 play 重建, 故每次现取(可能为 null: 未 play)。
    avox::ISurfaceRender* getSurfaceRenderRaw() override { return player ? player->getSurfaceRender() : nullptr; }

protected:
    static void _bind_methods();
    void _notification(int p_what);

private:
    friend class PlayerOb;

    // avox 播放器
    avox::IMediaPlayer *player = nullptr;
    avox::IMediaPlayerOb *playerOb = nullptr;

    // 纹理绑定在 ISurfaceRender 上(可复用于 SourcePlayer 等),
    // MediaPlayer 只持有一个实例并委托 get_texture()
    class SurfaceTextureBridge *surfaceBridge = nullptr;

    // 配置缓存 (open 前设置)
    String url;
    bool hardDecode = true;
    int ioPlan = 0;  // avox::IoPlan 缓存 (0=none 走 auto); play() 重建播放器后由 createPlayer 重放
    bool gpuPassthrough = true;
    float volume = 1.0f;
    // 预设选项暂存 (createPlayer 时落库; play 前设置的场景)
    Dictionary pendingOptions;

    // 线程安全的状态缓存
    std::atomic<int> stateCache{0};
    mutable std::atomic<double> durationCache{0};
    std::atomic<double> positionCache{0};
    std::atomic<int> fpsCache{0};

    void createPlayer();
    void destroyPlayer();
    void processFrame();
    void applySourceInfo();

    // 源信息(onReady 回调写入, 主线程消费)
    std::atomic<bool> sourceReady{false};
    std::atomic<int> videoW{0};
    std::atomic<int> videoH{0};
    // 源色彩空间编码 (encodeColorSpace, -1=未知)。CPU 回退路径 shader 的解码矩阵
    std::atomic<int> colorSpaceCode{-1};
};
} // namespace godot
