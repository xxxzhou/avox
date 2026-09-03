#include "player.h"
#include "surface.h"
#include "gpu_passthrough.h"
#include "option.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>

#include <avox/AvoxCodec.h>
#include <avox/AvoxMuxer.h>  // IoPlan enum

#include <string>

namespace godot {

// ── avox 播放器观察者 (接收 avox 回调, 转发到主线程) ──
class PlayerOb : public avox::IMediaPlayerOb {
public:
    MediaPlayer *owner = nullptr;

    void onStateChange(avox::PlayerState preState, avox::PlayerState state) override {
        if (owner) {
            owner->stateCache.store(static_cast<int>(state));
            owner->call_deferred("emit_signal", "state_changed", static_cast<int>(state));
        }
    }
    void onReady() override {
        if (owner && owner->player) {
            // onReady 时 sourceInfo 一定有效,取视频尺寸缓存(主线程消费)
            avox::ISourceInfo *info = owner->player->getSourceInfo();
            if (info && info->videoSize() > 0) {
                avox::VTrackDesc vd = info->getVideoDesc(0);
                owner->videoW.store(vd.desc.width);
                owner->videoH.store(vd.desc.height);
            }
            owner->sourceReady.store(true);
            owner->call_deferred("emit_signal", "ready");
        }
    }
    void onComplete() override {
        if (owner) {
            owner->call_deferred("emit_signal", "completed");
        }
    }
    void onIoError(avox::AVError error, const char *msg) override {
        if (owner) {
            owner->call_deferred("emit_signal", "io_error", static_cast<int>(error));
        }
    }
    void onDecodeError(avox::TrackType trackType, avox::DecodeResult error) override {
        if (owner) {
            owner->call_deferred("emit_signal", "decode_error", static_cast<int>(error));
        }
    }
    void onSeek() override {
        if (owner) {
            owner->call_deferred("emit_signal", "seeked");
        }
    }
    void onPause() override {}
    void onResume() override {}
    void onClose() override {}
};

// ── MediaPlayer ──

MediaPlayer::MediaPlayer() {
    surfaceBridge = new SurfaceTextureBridge();
}

MediaPlayer::~MediaPlayer() {
    destroyPlayer();
    delete surfaceBridge;
}

void MediaPlayer::_notification(int p_what) {
    switch (p_what) {
    case NOTIFICATION_READY:
        set_process(true);  // 接通每帧 NOTIFICATION_PROCESS -> processFrame -> update
        break;
    case NOTIFICATION_PROCESS:
        processFrame();
        if (player) {
            positionCache.store(player->getPosition());
            fpsCache.store(player->getFps());
        }
        break;
    case NOTIFICATION_EXIT_TREE:
        stop();
        break;
    }
}

void MediaPlayer::createPlayer() {
    if (player) return;
    // 主线程: Godot 已完全初始化, RenderingServer 单例可用。
    // 在此延迟初始化 GPU 直通 (GDExtension SCENE 级太早, 单例未注册)。
    avox_gpu_passthrough_init();
    player = avox::createMediaPlayer();
    playerOb = new PlayerOb();
    static_cast<PlayerOb *>(playerOb)->owner = this;
    avox::addMediaPlayerOb(player, playerOb);
    //配置
    player->setHardDecode(hardDecode);
    // 预设选项落库 (set_option 在首次 play 前存的键; 此时 open 命令尚未入队)
    if (pendingOptions.size() > 0) {
        Ref<AvoxOption> opt = AvoxOption::from_native_borrowed(player->getOption());
        Array keys = pendingOptions.keys();
        for (int i = 0; i < keys.size(); ++i) {
            opt->set(String(keys[i]), pendingOptions[keys[i]]);
        }
        pendingOptions.clear();
    }
    surfaceBridge->setGpuPassthroughEnabled(gpuPassthrough);
    //视频帧桥接: 纹理绑定在 ISurfaceRender 上, 可复用于 SourcePlayer 等
    surfaceBridge->bindSurface(player->getSurfaceRender());
    //音频
    auto *audio = player->getAudioRender();
    audio->setVolume(volume);
}

void MediaPlayer::destroyPlayer() {
    if (!player) return;
    //先解绑纹理桥接, 再关播放器(解绑需要 surface render 仍有效)
    surfaceBridge->unbindSurface();
    player->close();
    avox::removeMediaPlayerOb(player, playerOb);
    delete playerOb;
    playerOb = nullptr;
    //销毁 avox 播放器: ~MediaPlayer 先 stopTask(join 命令线程), 随后 unique_ptr 成员析构
    //(videoTracks → WindowRender → vkVideoRender → VkOutputLayer → sharedImage)。
    //渲染线程已在 WindowRender 析构的 stop() 里 join, 故 sharedImage 释放时无并发 cmd。
    //createMediaPlayer() 是裸 new 且无 destroy API, delete 是唯一释放路径(基类析构 virtual)。
    delete player;
    player = nullptr;
    stateCache.store(0);
    durationCache.store(0);
    positionCache.store(0);
    fpsCache.store(0);
    //复位源信息, 新 session 等 onReady 重新喂尺寸
    sourceReady.store(false);
    videoW.store(0);
    videoH.store(0);
}

void MediaPlayer::play(const String &p_url) {
    if (!p_url.is_empty()) {
        url = p_url;
    }
    if (url.is_empty()) return;
    if (player) {
        //重开新源必须销毁并重建 avox 播放器, 不能复用。原因:
        //VkOutputLayer/sharedImage/bVkInterop 跨 close/open 复用(cmdClose 不重建 videoTracks),
        //再次 enableVkOutput 时 createExportable 会同步释放上一 session 的导出 VkImage,
        //而重启的渲染线程其已录 cmd 仍引用该 image → use-after-free 崩溃(signal 11)。
        //重建后 VkOutputLayer 是全新状态(sharedImage 空, bVkInterop=false), enableVkOutput
        //与首次完全等价。surfaceBridge 复用, createPlayer 内重新绑新 surface。
        destroyPlayer();
    }
    createPlayer();
    player->open(url.utf8().get_data());
}

void MediaPlayer::stop() {
    if (player) {
        player->close();
    }
}

void MediaPlayer::seek(int64_t p_pos_ms) {
    if (player) {
        player->seek(p_pos_ms);
        // 立即同步目标到缓存: positionCache 本由 NOTIFICATION_PROCESS 每帧刷新,
        // 若只靠下一帧刷新, 松手瞬间进度条读到的是上一帧的旧值(前一次 seek 目标) →
        // 表现为"先回退到旧值再跳回新目标"。seek 时 avox 侧 bSeeking=true, getPosition 即报 seekPts,
        // 所以这里直接写 p_pos_ms 与 avox 语义一致。
        positionCache.store(p_pos_ms);
    }
}

void MediaPlayer::pause() {
    if (player) {
        player->pause();
    }
}

void MediaPlayer::resume() {
    if (player) {
        player->resume();
    }
}

void MediaPlayer::setSpeed(double p_speed) {
    if (player) {
        player->speed(p_speed);
    }
}

bool MediaPlayer::checkPlaying() const {
    return stateCache.load() == static_cast<int>(avox::PlayerState::playing);
}

int MediaPlayer::getState() const {
    return stateCache.load();
}

double MediaPlayer::getDuration() const {
    if (player) {
        durationCache.store(player->getDuration());
    }
    return durationCache.load();
}

double MediaPlayer::getPosition() const {
    return positionCache.load();
}

double MediaPlayer::getProgress() const {
    double dur = getDuration();
    if (dur <= 0) return 0.0;
    return positionCache.load() / dur;
}

int MediaPlayer::getFps() const {
    return fpsCache.load();
}

// 色彩空间 -> "BT.709 limited" 风格可读串
static std::string colorSpaceStr(const avox::ColorSpaceDesc &cs) {
    const char *s = "BT.601";
    if (cs.standard == avox::YuvStandard::bt709) {
        s = "BT.709";
    } else if (cs.standard == avox::YuvStandard::bt2020) {
        s = "BT.2020";
    }
    return std::string(s) + (cs.range == avox::YuvRange::full ? " full" : " limited");
}

// 打包媒体/编解码信息给 GDScript (工具菜单的"媒体信息"/"编解码信息"面板)
Dictionary MediaPlayer::getMediaInfo() const {
    Dictionary info;
    info["url"] = url;
    info["duration_ms"] = getDuration();
    info["can_seek"] = false;
    Array videos;
    Array audios;
    info["videos"] = videos;
    info["audios"] = audios;
    if (!player) return info;
    avox::ISourceInfo *si = player->getSourceInfo();
    if (!si) return info;
    info["can_seek"] = si->canSeek();
    for (int32_t i = 0; i < si->videoSize(); ++i) {
        avox::VTrackDesc vd = si->getVideoDesc(i);
        Dictionary d;
        d["codec"] = String(avox::getVCodecName(vd.codecId));
        d["width"] = vd.desc.width;
        d["height"] = vd.desc.height;
        d["fps"] = vd.desc.fps;
        d["pixel_format"] = String(avox::getYuvTypeStr(vd.desc.type));
        d["color_space"] = String(colorSpaceStr(vd.desc.colorSpace).c_str());
        videos.push_back(d);
    }
    for (int32_t i = 0; i < si->audioSize(); ++i) {
        avox::ATrackDesc ad = si->getAudioDesc(i);
        Dictionary d;
        d["codec"] = String(avox::getACodecName(ad.codecId));
        d["sample_rate"] = ad.desc.sampleRate;
        d["channels"] = static_cast<int>(ad.desc.channels);
        d["format"] = String(avox::getAudioFormatStr(ad.desc.format));
        audios.push_back(d);
    }
    return info;
}

void MediaPlayer::setIoPlan(int p_plan) {
    if (player) {
        // IoPlan: zlmediakit=1, ffmpeg=2。注释 "下次打开启用" — 立即写入, 下次 open() 生效。
        player->setIoPlan(static_cast<avox::IoPlan>(p_plan));
    }
}

Ref<AvoxOption> MediaPlayer::getOption() {
    if (!player) return Ref<AvoxOption>();
    // 借用包装 (不 owned): 由 player 持有, MediaPlayer 仅暴露给 GDScript。
    return AvoxOption::from_native_borrowed(player->getOption());
}

void MediaPlayer::setOption(const String &p_key, const Variant &p_value) {
    if (player) {
        // 播放器已存在: 立即写入 (下次 open 生效)
        Ref<AvoxOption> opt = AvoxOption::from_native_borrowed(player->getOption());
        if (opt.is_valid()) opt->set(p_key, p_value);
        return;
    }
    // 首次 play 前: 暂存, createPlayer 时统一落库 (open 命令在 createPlayer 之后入队)
    pendingOptions[p_key] = p_value;
}

void MediaPlayer::setUrl(const String &p_url) {
    url = p_url;
}

String MediaPlayer::getUrl() const {
    return url;
}

void MediaPlayer::setGpuPassthrough(bool p_enable) {
    gpuPassthrough = p_enable;
    if (surfaceBridge) {
        surfaceBridge->setGpuPassthroughEnabled(p_enable);
    }
}

bool MediaPlayer::getGpuPassthrough() const {
    return gpuPassthrough;
}

void MediaPlayer::setHardDecode(bool p_enable) {
    hardDecode = p_enable;
    if (player) {
        player->setHardDecode(p_enable);
    }
}

bool MediaPlayer::getHardDecode() const {
    return hardDecode;
}

void MediaPlayer::setVolume(float p_volume) {
    volume = p_volume;
    if (player) {
        player->getAudioRender()->setVolume(p_volume);
    }
}

float MediaPlayer::getVolume() const {
    return volume;
}

Ref<Texture2D> MediaPlayer::getTexture() const {
    if (surfaceBridge) {
        return surfaceBridge->getTexture();
    }
    return Ref<Texture2D>();
}

void MediaPlayer::processFrame() {
    applySourceInfo();
    if (surfaceBridge) {
        surfaceBridge->update();
    }
}

void MediaPlayer::applySourceInfo() {
    // onReady 后首次把视频真实尺寸喂给 GPU 直通路径
    // (enableVkOutput 与 VkImage 导入都以此为准, 保证 extent 一致)
    if (!sourceReady.load() || !surfaceBridge) return;
    int w = videoW.load();
    int h = videoH.load();
    if (w > 0 && h > 0) {
        surfaceBridge->setVideoSize(w, h);
    }
}

void MediaPlayer::_bind_methods() {
    //方法
    ClassDB::bind_method(D_METHOD("play", "url"), &MediaPlayer::play, DEFVAL(""));
    ClassDB::bind_method(D_METHOD("stop"), &MediaPlayer::stop);
    ClassDB::bind_method(D_METHOD("seek", "pos_ms"), &MediaPlayer::seek);
    ClassDB::bind_method(D_METHOD("pause"), &MediaPlayer::pause);
    ClassDB::bind_method(D_METHOD("resume"), &MediaPlayer::resume);
    ClassDB::bind_method(D_METHOD("set_speed", "speed"), &MediaPlayer::setSpeed);

    //属性 setter/getter —— ADD_PROPERTY 引用的方法必须显式 bind, 否则报
    //"Setter method not found", GDScript 里 player.url = ... 会失败
    ClassDB::bind_method(D_METHOD("set_url", "url"), &MediaPlayer::setUrl);
    ClassDB::bind_method(D_METHOD("get_url"), &MediaPlayer::getUrl);
    ClassDB::bind_method(D_METHOD("set_hard_decode", "enable"), &MediaPlayer::setHardDecode);
    ClassDB::bind_method(D_METHOD("get_hard_decode"), &MediaPlayer::getHardDecode);
    ClassDB::bind_method(D_METHOD("set_gpu_passthrough", "enable"), &MediaPlayer::setGpuPassthrough);
    ClassDB::bind_method(D_METHOD("get_gpu_passthrough"), &MediaPlayer::getGpuPassthrough);
    ClassDB::bind_method(D_METHOD("set_volume", "volume"), &MediaPlayer::setVolume);
    ClassDB::bind_method(D_METHOD("get_volume"), &MediaPlayer::getVolume);

    ClassDB::bind_method(D_METHOD("is_playing"), &MediaPlayer::checkPlaying);
    ClassDB::bind_method(D_METHOD("get_state"), &MediaPlayer::getState);
    ClassDB::bind_method(D_METHOD("get_duration"), &MediaPlayer::getDuration);
    ClassDB::bind_method(D_METHOD("get_position"), &MediaPlayer::getPosition);
    ClassDB::bind_method(D_METHOD("get_progress"), &MediaPlayer::getProgress);
    ClassDB::bind_method(D_METHOD("get_fps"), &MediaPlayer::getFps);
    ClassDB::bind_method(D_METHOD("get_media_info"), &MediaPlayer::getMediaInfo);
    ClassDB::bind_method(D_METHOD("set_io_plan", "plan"), &MediaPlayer::setIoPlan);
    ClassDB::bind_method(D_METHOD("get_option"), &MediaPlayer::getOption);
    ClassDB::bind_method(D_METHOD("set_option", "key", "value"), &MediaPlayer::setOption);
    ClassDB::bind_method(D_METHOD("get_texture"), &MediaPlayer::getTexture);

    //属性
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "url"), "set_url", "get_url");
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "hard_decode"), "set_hard_decode", "get_hard_decode");
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "gpu_passthrough"), "set_gpu_passthrough", "get_gpu_passthrough");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "volume", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_volume", "get_volume");

    //信号
    ADD_SIGNAL(MethodInfo("state_changed", PropertyInfo(Variant::INT, "state")));
    // "ready" 是 Node 内置信号, MediaPlayer 继承 Node 已有, 不要再 ADD_SIGNAL (否则报 already has signal 'ready')
    ADD_SIGNAL(MethodInfo("completed"));
    ADD_SIGNAL(MethodInfo("io_error", PropertyInfo(Variant::INT, "code")));
    ADD_SIGNAL(MethodInfo("decode_error", PropertyInfo(Variant::INT, "code")));
    ADD_SIGNAL(MethodInfo("seeked", PropertyInfo(Variant::INT, "pos_ms")));
}

} // namespace godot
