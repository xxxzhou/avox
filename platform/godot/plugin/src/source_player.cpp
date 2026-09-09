#include "source_player.h"
#include "surface.h"
#include "gpu_passthrough.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <avox/AvoxCodec.h>

namespace godot {

// ── 平台默认设备管理器单例缓存 (进程级, 设备指针生命周期归 manager) ──
static avox::IVideoManager *videoMgr() {
    static avox::IVideoManager *m = avox::getVideoManager(avox::getDefaltVideoSdk());
    return m;
}
static avox::IAudioManager *audioMgr() {
    static avox::IAudioManager *m = avox::getAudioManager(avox::getDefaltAudioSdk());
    return m;
}

static String cstr(const char *s) {
    return String::utf8(s ? s : "");
}
static const char *vKindStr(avox::VDeviceKind k) {
    switch (k) {
    case avox::VDeviceKind::camera: return "camera";
    case avox::VDeviceKind::window: return "window";
    case avox::VDeviceKind::monitor: return "monitor";
    default: return "none";
    }
}
static const char *aKindStr(avox::ADeviceKind k) {
    switch (k) {
    case avox::ADeviceKind::mic: return "mic";
    case avox::ADeviceKind::loopback: return "loopback";
    default: return "none";
    }
}

// ── SourcePlayer 观察者 (IMediaPlayerOb → 主线程信号) ──
class SourcePlayerOb : public avox::IMediaPlayerOb {
public:
    SourcePlayer *owner = nullptr;

    void onStateChange(avox::PlayerState, avox::PlayerState state) override {
        if (owner) {
            owner->stateCache.store(static_cast<int>(state));
            owner->call_deferred("emit_signal", "state_changed", static_cast<int>(state));
        }
    }
    void onReady() override {
        if (owner && owner->player) {
            avox::ISourceInfo *info = owner->player->getSourceInfo();
            if (info && info->videoSize() > 0) {
                avox::VTrackDesc vd = info->getVideoDesc(0);
                owner->videoW.store(vd.desc.width);
                owner->videoH.store(vd.desc.height);
                owner->colorSpaceCode.store(encodeColorSpace(vd.desc.colorSpace));
            }
            owner->sourceReady.store(true);
            owner->call_deferred("emit_signal", "ready");  // Node 内置信号
        }
    }
    void onComplete() override {
        if (owner) owner->call_deferred("emit_signal", "completed");
    }
    void onIoError(avox::AVError error, const char *) override {
        if (owner) owner->call_deferred("emit_signal", "io_error", static_cast<int>(error));
    }
    void onDecodeError(avox::TrackType, avox::DecodeResult error) override {
        if (owner) owner->call_deferred("emit_signal", "decode_error", static_cast<int>(error));
    }
    void onSeek() override {
        if (owner) owner->call_deferred("emit_signal", "seeked");
    }
    void onPause() override {}
    void onResume() override {}
    void onClose() override {}
};

// ── SourcePlayer ──

SourcePlayer::SourcePlayer() {}

SourcePlayer::~SourcePlayer() {
    destroyPlayer();
    if (surfaceBridge) {
        delete surfaceBridge;
        surfaceBridge = nullptr;
    }
}

void SourcePlayer::_notification(int p_what) {
    switch (p_what) {
    case NOTIFICATION_READY:
        set_process(true);
        break;
    case NOTIFICATION_PROCESS:
        processFrame();
        if (player) {
            stateCache.store(static_cast<int>(player->getState()));
        }
        break;
    case NOTIFICATION_EXIT_TREE:
        close();
        break;
    }
}

void SourcePlayer::createPlayer() {
    if (player) return;
    avox_gpu_passthrough_init();
    player.reset(avox::createDevicePlayer());
    playerOb.reset(new SourcePlayerOb());
    static_cast<SourcePlayerOb *>(playerOb.get())->owner = this;
    avox::addSourcePlayerOb(player.get(), playerOb.get());
    // 桥只建一次并复用: 每次 open 新建会漏掉旧桥 (及其 SubViewport 子节点)
    if (!surfaceBridge) {
        surfaceBridge = new SurfaceTextureBridge();
        surfaceBridge->setOwnerNode(this);
    }
    surfaceBridge->bindSurface(player->getSurfaceRender());
}

void SourcePlayer::destroyPlayer() {
    if (!player) return;
    if (surfaceBridge) surfaceBridge->unbindSurface();
    avox::removeSourcePlayerOb(player.get(), playerOb.get());
    playerOb.reset();
    player.reset();  // DevicePlayerDeleter: close + delete
    stateCache.store(0);
    sourceReady.store(false);
    colorSpaceCode.store(-1);
    videoW.store(0);
    videoH.store(0);
}

bool SourcePlayer::set_video_source(const String &p_id) {
    avox::IVideoSource *d = DeviceManager::find_video(p_id);
    if (!d) return false;
    createPlayer();
    player->setVideoSource(d);
    return true;
}

bool SourcePlayer::set_video_source_index(int64_t p_index) {
    avox::IVideoSource *d = DeviceManager::video_at(p_index);
    if (!d) return false;
    createPlayer();
    player->setVideoSource(d);
    return true;
}

bool SourcePlayer::set_audio_source(const String &p_id) {
    avox::IAudioSource *d = DeviceManager::find_audio(p_id);
    if (!d) return false;
    createPlayer();
    player->setAudioSource(d);
    return true;
}

bool SourcePlayer::set_audio_source_index(int64_t p_index) {
    avox::IAudioSource *d = DeviceManager::audio_at(p_index);
    if (!d) return false;
    createPlayer();
    player->setAudioSource(d);
    return true;
}

void SourcePlayer::open() {
    if (!player) createPlayer();
    if (player) player->open();
}

void SourcePlayer::close() {
    if (player) player->close();
}

int SourcePlayer::get_state() const {
    return stateCache.load();
}

bool SourcePlayer::is_open() const {
    return (bool)player;
}

Ref<Texture2D> SourcePlayer::get_texture() const {
    if (surfaceBridge) return surfaceBridge->getTexture();
    return Ref<Texture2D>();
}

Dictionary SourcePlayer::get_source_info() const {
    Dictionary info;
    info["can_seek"] = false;
    Array videos, audios;
    info["videos"] = videos;
    info["audios"] = audios;
    if (!player) return info;
    avox::ISourceInfo *si = player->getSourceInfo();
    if (!si) return info;
    info["can_seek"] = si->canSeek();
    for (int32_t i = 0; i < si->videoSize(); ++i) {
        avox::VTrackDesc vd = si->getVideoDesc(i);
        Dictionary d;
        d["codec"] = cstr(avox::getVCodecName(vd.codecId));
        d["width"] = vd.desc.width;
        d["height"] = vd.desc.height;
        d["fps"] = vd.desc.fps;
        videos.push_back(d);
    }
    for (int32_t i = 0; i < si->audioSize(); ++i) {
        avox::ATrackDesc ad = si->getAudioDesc(i);
        Dictionary d;
        d["codec"] = cstr(avox::getACodecName(ad.codecId));
        d["sample_rate"] = ad.desc.sampleRate;
        d["channels"] = static_cast<int>(ad.desc.channels);
        audios.push_back(d);
    }
    return info;
}

void SourcePlayer::processFrame() {
    applySourceInfo();
    if (surfaceBridge) surfaceBridge->update();
}

void SourcePlayer::applySourceInfo() {
    // onReady 后把真实视频尺寸喂 GPU 直通路径 (extent 一致)
    if (!sourceReady.load() || !surfaceBridge) return;
    int w = videoW.load();
    int h = videoH.load();
    if (w > 0 && h > 0) surfaceBridge->setVideoSize(w, h);
    // 色彩空间: 幂等, 桥内部只在变化时下发
    int cs = colorSpaceCode.load();
    if (cs >= 0) surfaceBridge->setColorSpace(decodeColorSpace(cs));
}

void SourcePlayer::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_video_source", "id"), &SourcePlayer::set_video_source);
    ClassDB::bind_method(D_METHOD("set_video_source_index", "index"), &SourcePlayer::set_video_source_index);
    ClassDB::bind_method(D_METHOD("set_audio_source", "id"), &SourcePlayer::set_audio_source);
    ClassDB::bind_method(D_METHOD("set_audio_source_index", "index"), &SourcePlayer::set_audio_source_index);
    ClassDB::bind_method(D_METHOD("open"), &SourcePlayer::open);
    ClassDB::bind_method(D_METHOD("close"), &SourcePlayer::close);
    ClassDB::bind_method(D_METHOD("get_state"), &SourcePlayer::get_state);
    ClassDB::bind_method(D_METHOD("get_source_info"), &SourcePlayer::get_source_info);
    ClassDB::bind_method(D_METHOD("get_texture"), &SourcePlayer::get_texture);
    ClassDB::bind_method(D_METHOD("is_open"), &SourcePlayer::is_open);

    // "ready" 是 Node 内置信号, 不要 ADD_SIGNAL
    ADD_SIGNAL(MethodInfo("state_changed", PropertyInfo(Variant::INT, "state")));
    ADD_SIGNAL(MethodInfo("completed"));
    ADD_SIGNAL(MethodInfo("io_error", PropertyInfo(Variant::INT, "code")));
    ADD_SIGNAL(MethodInfo("decode_error", PropertyInfo(Variant::INT, "code")));
    ADD_SIGNAL(MethodInfo("seeked"));
}

// ── DeviceManager ──

DeviceManager::DeviceManager() {}
DeviceManager::~DeviceManager() {}

Array DeviceManager::list_video_devices() {
    Array out;
    avox::IVideoManager *m = videoMgr();
    if (!m) return out;
    int32_t n = m->getDeviceCount();
    for (int32_t i = 0; i < n; ++i) {
        avox::IVideoSource *d = m->getDevice(i);
        if (!d) continue;
        Dictionary info;
        info["index"] = (int64_t)i;
        info["name"] = cstr(d->getDeviceName());
        info["id"] = cstr(d->getDeviceId());
        int kind = (int)d->getDeviceKind();
        info["kind"] = kind;
        info["kind_str"] = String(vKindStr((avox::VDeviceKind)kind));
        out.push_back(info);
    }
    return out;
}

Array DeviceManager::list_audio_devices() {
    Array out;
    avox::IAudioManager *m = audioMgr();
    if (!m) return out;
    int32_t n = m->getDeviceCount();
    for (int32_t i = 0; i < n; ++i) {
        avox::IAudioSource *d = m->getDevice(i);
        if (!d) continue;
        Dictionary info;
        info["index"] = (int64_t)i;
        info["name"] = cstr(d->getDeviceName());
        info["id"] = cstr(d->getDeviceId());
        int kind = (int)d->getDeviceKind();
        info["kind"] = kind;
        info["kind_str"] = String(aKindStr((avox::ADeviceKind)kind));
        out.push_back(info);
    }
    return out;
}

void DeviceManager::refresh_video_devices() {
    if (avox::IVideoManager *m = videoMgr()) m->refreshDevices();
}

void DeviceManager::refresh_audio_devices() {
    if (avox::IAudioManager *m = audioMgr()) m->refreshDevices();
}

avox::IVideoSource *DeviceManager::find_video(const String &p_id) {
    avox::IVideoManager *m = videoMgr();
    if (!m || p_id.is_empty()) return nullptr;
    return m->findDevice(p_id.utf8().get_data());
}

avox::IAudioSource *DeviceManager::find_audio(const String &p_id) {
    avox::IAudioManager *m = audioMgr();
    if (!m || p_id.is_empty()) return nullptr;
    return m->findDevice(p_id.utf8().get_data());
}

avox::IVideoSource *DeviceManager::video_at(int64_t p_index) {
    avox::IVideoManager *m = videoMgr();
    if (!m || p_index < 0) return nullptr;
    return m->getDevice((int32_t)p_index);
}

avox::IAudioSource *DeviceManager::audio_at(int64_t p_index) {
    avox::IAudioManager *m = audioMgr();
    if (!m || p_index < 0) return nullptr;
    return m->getDevice((int32_t)p_index);
}

void DeviceManager::_bind_methods() {
    ClassDB::bind_static_method("DeviceManager", D_METHOD("list_video_devices"), &DeviceManager::list_video_devices);
    ClassDB::bind_static_method("DeviceManager", D_METHOD("list_audio_devices"), &DeviceManager::list_audio_devices);
    ClassDB::bind_static_method("DeviceManager", D_METHOD("refresh_video_devices"), &DeviceManager::refresh_video_devices);
    ClassDB::bind_static_method("DeviceManager", D_METHOD("refresh_audio_devices"), &DeviceManager::refresh_audio_devices);
    // find_video/find_audio/video_at/audio_at 是 C++ 内部用 (SourcePlayer), 不绑定 GDScript
}

} // namespace godot
