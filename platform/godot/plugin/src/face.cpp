// face.cpp — 虚拟人面部追踪 Godot 封装 (FaceNode)
// 行为基准: TtsNode (voice.cpp). PCM 进 (feed_pcm) → ARKit52 blendshape 出 (face_blendshape)。
//
// 线程模型:
//   FaceOb 回调在 avox_avatar worker 线程 → 52×float 复制成 PackedFloat32Array
//   经 call_deferred("emit_signal", ...) 投递 Godot 主线程 (跨线程安全)。
//   模型加载在 loadThread (385MB, 秒级), 完成发 face_ready。

#include "face.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

namespace godot {

namespace {

// 宽字面量 → Godot String (UTF-8)。插件无 /utf-8, 窄字面量是本地编码 (Win=GBK),
// String(...) 按 Latin-1、String::utf8(...) 按 UTF-8 读都乱码; 宽字面量编码无关,
// 是插件内中文文案唯一可靠写法 (移植自 voice.cpp)。
String wstrToStr(const wchar_t *ws) {
    if (!ws || !*ws) return String();
#ifdef _WIN32
    int len = WideCharToMultiByte(CP_UTF8, 0, ws, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 1) return String();
    std::string s((size_t)len - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, ws, -1, &s[0], len, nullptr, nullptr);
    return String::utf8(s.c_str());
#else
    std::string u8;
    for (const wchar_t *p = ws; *p; ++p) {
        uint32_t c = (uint32_t)*p;
        if (c < 0x80) u8 += char(c);
        else if (c < 0x800) { u8 += char(0xC0 | (c >> 6)); u8 += char(0x80 | (c & 0x3F)); }
        else if (c < 0x10000) { u8 += char(0xE0 | (c >> 12)); u8 += char(0x80 | ((c >> 6) & 0x3F)); u8 += char(0x80 | (c & 0x3F)); }
        else { u8 += char(0xF0 | (c >> 18)); u8 += char(0x80 | ((c >> 12) & 0x3F)); u8 += char(0x80 | ((c >> 6) & 0x3F)); u8 += char(0x80 | (c & 0x3F)); }
    }
    return String::utf8(u8.c_str());
#endif
}

} // namespace

// ============================================================
// FaceOb (blendshape 观察者, 实现 IAudioFaceOb)
// ============================================================

// 回调在 avox_avatar worker 线程: 52×float 深拷贝成 PackedFloat32Array (raw 仅回调内有效),
// 经 call_deferred 投递主线程。对齐 TtsOb::onTtsAudio。
class FaceOb : public avox::IAudioFaceOb {
public:
    FaceNode *owner = nullptr;
    void onFaceDesc(const avox::FaceDesc &desc) override {
        if (owner)
            owner->call_deferred("emit_signal", "face_desc", desc.fps, desc.blendshapeCount);
    }
    void onFaceBlendshape(const avox::AvoxData &raw52, int64_t pts, bool final) override {
        if (!owner) return;
        PackedFloat32Array arr;
        int n = raw52.size / int32_t(sizeof(float));
        if (n > 0 && raw52.data) {
            arr.resize(n);
            std::memcpy(arr.ptrw(), raw52.data, raw52.size);
        }
        owner->call_deferred("emit_signal", "face_blendshape", arr, pts, final);
    }
    void onFaceError(const char *err) override {
        if (owner && err)
            owner->call_deferred("emit_signal", "error", String::utf8(err));
    }
};

// ============================================================
// FaceNode
// ============================================================

FaceNode::~FaceNode() {
    stop();  // join loadThread + 排空 worker
    if (face && faceOb) avox::removeAudioFaceOb(face.get(), faceOb.get());  // 观察者先于 face 释放
}

void FaceNode::start() {
    if (!face) {
        face.reset(avox::createAudioFace(avox::AudioFaceType::wav2arkit));
        if (!face) {
            call_deferred("emit_signal", "error",
                          wstrToStr(L"创建面部推理器失败 (avox_avatar 插件未注册? 模型未下载?)"));
            return;
        }
        // 默认 s16 mono 22050 (kokoro); GDScript 在 tts_desc 时 set_audio_desc 校正采样率
        avox::AudioDesc desc{};
        desc.format = avox::AudioFormat::AVOX_AUDIO_S16;
        desc.channels = (uint16_t)channels;
        desc.sampleRate = sampleRate;
        face->setAudioDesc(desc);
        faceOb = std::make_unique<FaceOb>();
        static_cast<FaceOb *>(faceOb.get())->owner = this;
        avox::addAudioFaceOb(face.get(), faceOb.get());

        // 首次创建才开后台加载线程 (wav2arkit 385MB, 秒级, 不阻塞主线程)。
        // loading()==running(): face->start() 拉起 worker, 模型在 worker 内 initEngine;
        // worker 起来后发 face_ready (对齐 TtsNode 模式; 失败由 onFaceError/error 兜底)。
        loadStop.store(false);
        loadThread = std::thread([this]() {
            face->start();
            auto t0 = std::chrono::steady_clock::now();
            while (!loadStop.load() && !face->loading() &&
                   std::chrono::steady_clock::now() - t0 < std::chrono::seconds(60)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
            }
            if (loadStop.load()) return;
            if (face->loading()) {
                call_deferred("emit_signal", "face_ready");
            } else {
                call_deferred("emit_signal", "error",
                              wstrToStr(L"面部模型加载超时 (用 fetch_assets 下载 wav2arkit)"));
            }
        });
    } else {
        // 后续轮: 幂等重启任务, 不重载模型
        face->start();
    }
}

void FaceNode::stop() {
    loadStop.store(true);
    if (loadThread.joinable()) loadThread.join();
    if (!face) return;
    // face->stop() 排空已入队 PCM + join worker; 只 stop() 不销毁: 模型跨轮复用。
    face->stop();
}

bool FaceNode::loading() {
    return face ? face->loading() : false;
}

void FaceNode::setAudioDesc(int p_sample_rate, int p_channels) {
    if (p_sample_rate > 0) sampleRate = p_sample_rate;
    if (p_channels > 0) channels = p_channels;
    if (!face) return;
    avox::AudioDesc desc{};
    desc.format = avox::AudioFormat::AVOX_AUDIO_S16;   // TTS PCM 为 s16
    desc.channels = (uint16_t)channels;
    desc.sampleRate = sampleRate;
    face->setAudioDesc(desc);
}

void FaceNode::feedPcm(PackedByteArray p_data, int64_t p_pts) {
    if (!face) return;
    avox::AvoxData ad;
    ad.data = p_data.ptrw();   // 引用外部数据; feed 同步入队 (reshaper 内深拷贝), 调用期间 pba 存活
    ad.size = (int32_t)p_data.size();
    ad.bRef = true;
    face->feed(ad, p_pts);
}

void FaceNode::_notification(int p_what) {
    if (p_what == NOTIFICATION_EXIT_TREE) stop();
}

void FaceNode::_bind_methods() {
    ClassDB::bind_method(D_METHOD("start"), &FaceNode::start);
    ClassDB::bind_method(D_METHOD("stop"), &FaceNode::stop);
    ClassDB::bind_method(D_METHOD("loading"), &FaceNode::loading);
    ClassDB::bind_method(D_METHOD("set_audio_desc", "sample_rate", "channels"), &FaceNode::setAudioDesc);
    ClassDB::bind_method(D_METHOD("feed_pcm", "data", "pts"), &FaceNode::feedPcm);

    // face_blendshape: blendshape=52×float[0,1], pts=帧时间戳(ms), final=本次 feed 末帧
    ADD_SIGNAL(MethodInfo("face_desc",
                          PropertyInfo(Variant::INT, "fps"),
                          PropertyInfo(Variant::INT, "blendshape_count")));
    ADD_SIGNAL(MethodInfo("face_blendshape",
                          PropertyInfo(Variant::PACKED_FLOAT32_ARRAY, "blendshape"),
                          PropertyInfo(Variant::INT, "pts"),
                          PropertyInfo(Variant::BOOL, "final")));
    ADD_SIGNAL(MethodInfo("face_ready"));
    ADD_SIGNAL(MethodInfo("error", PropertyInfo(Variant::STRING, "msg")));
}

} // namespace godot
