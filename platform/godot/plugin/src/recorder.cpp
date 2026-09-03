#include "recorder.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <avox/AvoxCodec.h>

namespace godot {

// ── Recorder 观察者 (IRecorderOb → 主线程信号) ──
class RecorderOb : public avox::IRecorderOb {
public:
    MediaRecorder *owner = nullptr;

    void onStateChange(avox::RecorderState, avox::RecorderState state) override {
        if (owner) {
            owner->stateCache.store(static_cast<int>(state));
            owner->call_deferred("emit_signal", "state_changed", static_cast<int>(state));
        }
    }
    void onProgress(const avox::RecorderProgress &p) override {
        if (owner) {
            Dictionary d;
            d["current_ms"] = (int64_t)p.currentTimeMs;
            d["total_ms"] = (int64_t)p.totalTimeMs;
            owner->call_deferred("emit_signal", "progress", d);
        }
    }
    void onIoError(avox::AVError error, const char *) override {
        if (owner) owner->call_deferred("emit_signal", "io_error", static_cast<int>(error));
    }
    void onEncodeError(avox::TrackType tt, avox::EncodeResult error) override {
        if (owner) owner->call_deferred("emit_signal", "encode_error", static_cast<int>(tt), static_cast<int>(error));
    }
    void onComplete() override {
        if (owner) owner->call_deferred("emit_signal", "completed");
    }
};

// ── MediaRecorder ──

MediaRecorder::MediaRecorder() {}

MediaRecorder::~MediaRecorder() {
    destroyRecorder();
}

void MediaRecorder::_notification(int p_what) {
    if (p_what == NOTIFICATION_EXIT_TREE) {
        close();
    }
}

void MediaRecorder::ensureRecorder() {
    if (recorder) return;
    recorder.reset(avox::createRecorder(bTranscode));
    recorderOb.reset(new RecorderOb());
    static_cast<RecorderOb *>(recorderOb.get())->owner = this;
    avox::addRecorderOb(recorder.get(), recorderOb.get());
    // 应用缓存配置 (open 前生效; 与 avox 默认一致也无害)
    // 注: IRecorder 接口无 setHardEncode (该方法在 IMediaMuxer, recorder 内部自带), 故不暴露
    recorder->setVideoCodec(static_cast<avox::VCodecId>(videoCodec));
    recorder->setAudioCodec(static_cast<avox::ACodecId>(audioCodec));
    recorder->setMuxerType(static_cast<avox::MuxerType>(muxerType));
    recorder->setIoPlan(static_cast<avox::IoPlan>(ioPlan));
}

void MediaRecorder::destroyRecorder() {
    if (!recorder) return;
    avox::removeRecorderOb(recorder.get(), recorderOb.get());
    recorderOb.reset();
    recorder.reset();  // RecorderDeleter: close + delete
    stateCache.store(0);
}

bool MediaRecorder::open(const String &p_input, const String &p_output) {
    if (p_input.is_empty()) return false;
    ensureRecorder();
    if (!recorder) return false;
    return recorder->open(p_input.utf8().get_data(), p_output.utf8().get_data());
}

void MediaRecorder::close() {
    if (recorder) recorder->close();
}

bool MediaRecorder::seek(int64_t p_pos_ms) {
    if (recorder) return recorder->seek(p_pos_ms);
    return false;
}

int MediaRecorder::get_state() const {
    if (recorder) return static_cast<int>(recorder->getState());
    return stateCache.load();
}

int64_t MediaRecorder::get_duration() const {
    if (recorder) return recorder->getDuration();
    return 0;
}

Dictionary MediaRecorder::get_source_info() const {
    Dictionary info;
    info["can_seek"] = false;
    Array videos, audios;
    info["videos"] = videos;
    info["audios"] = audios;
    if (!recorder) return info;
    avox::ISourceInfo *si = recorder->getSourceInfo();
    if (!si) return info;
    info["can_seek"] = si->canSeek();
    for (int32_t i = 0; i < si->videoSize(); ++i) {
        avox::VTrackDesc vd = si->getVideoDesc(i);
        Dictionary d;
        d["codec"] = String::utf8(avox::getVCodecName(vd.codecId));
        d["width"] = vd.desc.width;
        d["height"] = vd.desc.height;
        d["fps"] = vd.desc.fps;
        videos.push_back(d);
    }
    for (int32_t i = 0; i < si->audioSize(); ++i) {
        avox::ATrackDesc ad = si->getAudioDesc(i);
        Dictionary d;
        d["codec"] = String::utf8(avox::getACodecName(ad.codecId));
        d["sample_rate"] = ad.desc.sampleRate;
        d["channels"] = static_cast<int>(ad.desc.channels);
        audios.push_back(d);
    }
    return info;
}

// ── 配置 setter/getter ──

void MediaRecorder::set_transcode(bool p_b) {
    if (bTranscode == p_b) return;
    bTranscode = p_b;
    // transcode 是 createRecorder 的构造参数, 已有 recorder 须重建
    if (recorder) destroyRecorder();
}
bool MediaRecorder::get_transcode() const { return bTranscode; }

void MediaRecorder::set_video_codec(int64_t p_codec) {
    videoCodec = p_codec;
    if (recorder) recorder->setVideoCodec(static_cast<avox::VCodecId>(p_codec));
}
int64_t MediaRecorder::get_video_codec() const { return videoCodec; }

void MediaRecorder::set_audio_codec(int64_t p_codec) {
    audioCodec = p_codec;
    if (recorder) recorder->setAudioCodec(static_cast<avox::ACodecId>(p_codec));
}
int64_t MediaRecorder::get_audio_codec() const { return audioCodec; }

void MediaRecorder::set_muxer_type(int64_t p_t) {
    muxerType = p_t;
    if (recorder) recorder->setMuxerType(static_cast<avox::MuxerType>(p_t));
}
int64_t MediaRecorder::get_muxer_type() const { return muxerType; }

void MediaRecorder::set_io_plan(int64_t p_p) {
    ioPlan = p_p;
    if (recorder) recorder->setIoPlan(static_cast<avox::IoPlan>(p_p));
}
int64_t MediaRecorder::get_io_plan() const { return ioPlan; }

void MediaRecorder::_bind_methods() {
    ClassDB::bind_method(D_METHOD("open", "input", "output"), &MediaRecorder::open);
    ClassDB::bind_method(D_METHOD("close"), &MediaRecorder::close);
    ClassDB::bind_method(D_METHOD("seek", "pos_ms"), &MediaRecorder::seek);
    ClassDB::bind_method(D_METHOD("get_state"), &MediaRecorder::get_state);
    ClassDB::bind_method(D_METHOD("get_duration"), &MediaRecorder::get_duration);
    ClassDB::bind_method(D_METHOD("get_source_info"), &MediaRecorder::get_source_info);

    // setter/getter (ADD_PROPERTY 引用须显式 bind)
    ClassDB::bind_method(D_METHOD("set_transcode", "enable"), &MediaRecorder::set_transcode);
    ClassDB::bind_method(D_METHOD("get_transcode"), &MediaRecorder::get_transcode);
    ClassDB::bind_method(D_METHOD("set_video_codec", "codec"), &MediaRecorder::set_video_codec);
    ClassDB::bind_method(D_METHOD("get_video_codec"), &MediaRecorder::get_video_codec);
    ClassDB::bind_method(D_METHOD("set_audio_codec", "codec"), &MediaRecorder::set_audio_codec);
    ClassDB::bind_method(D_METHOD("get_audio_codec"), &MediaRecorder::get_audio_codec);
    ClassDB::bind_method(D_METHOD("set_muxer_type", "type"), &MediaRecorder::set_muxer_type);
    ClassDB::bind_method(D_METHOD("get_muxer_type"), &MediaRecorder::get_muxer_type);
    ClassDB::bind_method(D_METHOD("set_io_plan", "plan"), &MediaRecorder::set_io_plan);
    ClassDB::bind_method(D_METHOD("get_io_plan"), &MediaRecorder::get_io_plan);

    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "transcode"), "set_transcode", "get_transcode");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "video_codec"), "set_video_codec", "get_video_codec");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "audio_codec"), "set_audio_codec", "get_audio_codec");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "muxer_type"), "set_muxer_type", "get_muxer_type");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "io_plan"), "set_io_plan", "get_io_plan");

    ADD_SIGNAL(MethodInfo("state_changed", PropertyInfo(Variant::INT, "state")));
    ADD_SIGNAL(MethodInfo("progress", PropertyInfo(Variant::DICTIONARY, "info")));
    ADD_SIGNAL(MethodInfo("io_error", PropertyInfo(Variant::INT, "code")));
    ADD_SIGNAL(MethodInfo("encode_error", PropertyInfo(Variant::INT, "track"), PropertyInfo(Variant::INT, "code")));
    ADD_SIGNAL(MethodInfo("completed"));
}

} // namespace godot
