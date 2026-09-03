#pragma once

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>

#include <avox/AvoxAudio.h>
#include <avox/AvoxSource.h>
#include <avox/AvoxPlayer.h>

#include <atomic>
#include <memory>
#include <thread>

namespace godot {

// 前向声明 (Win32 钩子线程与热键配置, 实现放 voice.cpp 隔离 windows.h)
struct GlobalHotkeyImpl;

// ISourcePlayer 释放需先 close() —— SourcePlayer::~SourcePlayer 只 stopTask 不 close,
// 所以不能裸 delete; 自定义 deleter 保证 RAII 下 close 不被跳过。
struct SourcePlayerDeleter {
    void operator()(avox::ISourcePlayer *p) const {
        if (p) {
            p->close();
            delete p;
        }
    }
};

// 全局热键 (WH_KEYBOARD_LL): 同时注册两套热键 —— toggle(按一次切换) 与 hold(按住说话),
// 各自独立绑定。后台钩子线程 → toggled/pressed/released 信号;
// 钩子全吞热键 (含自动重复), 防修饰键污染注入; GDScript 侧编排录音状态机。
class GlobalHotkey : public Node {
    GDCLASS(GlobalHotkey, Node)

public:
    GlobalHotkey();
    ~GlobalHotkey();

    bool registerHotkey(const String &p_kind, const String &p_name);  // kind: "toggle"|"hold"
    void unregisterHotkey();
    void setCaptureMode(bool p_on);    // 设置热键期间置 true, 钩子不响应
    bool getCaptureMode() const;
    void setActiveMode(const String &p_kind);   // 单模式开关: 只有该模式热键响应 (kind: "toggle"|"hold")
    String getActiveMode() const;
    String getHotkeyDisplay(const String &p_kind) const;   // 规范化显示串 (Ctrl+Shift+F9)

protected:
    static void _bind_methods();
    void _notification(int p_what);

private:
    std::unique_ptr<GlobalHotkeyImpl> impl;   // PIMPL, 完整类型在 voice.cpp (dtor 在 .cpp 定义)
    void startThread();
    void stopThread();
};

// 麦克风采集 (createDevicePlayer + IAudioRender::openTap) → audio_desc/audio_frame 信号
// 音频帧在 tap 线程复制进 PackedByteArray 后经 call_deferred 投递到主线程。
// forward_to: 直接在 tap 线程喂给 SttNode (绕过主线程延迟), audio_desc 同步设置不丢帧。
class MicCapture : public Node {
    GDCLASS(MicCapture, Node)

public:
    MicCapture();
    ~MicCapture();

    bool start();
    void stop();
    PackedStringArray listDevices() const;
    void setDeviceIndex(int p_idx);
    int getDeviceIndex() const;
    void setFrameMs(int p_ms);     // 切片时长, 默认 100
    int getFrameMs() const;
    void forwardTo(Node *p_stt);   // 直喂 SttNode (tap 线程内 setAudioDesc + recognize, 无主线程延迟)
    float getAudioLevel() const;   // 最近一帧 RMS 电平 (0~1, 供 UI 显示)

protected:
    static void _bind_methods();
    void _notification(int p_what);

private:
    friend class MicTapOb;
    std::unique_ptr<avox::ISourcePlayer, SourcePlayerDeleter> player;  // close+delete
    std::unique_ptr<avox::IAudioTapOb> tapOb;
    Node *sttTarget = nullptr;     // forward_to 目标 (弱引用, 不拥有)
    std::atomic<float> audioLevel{0.0f};  // 最近帧 RMS (0~1, tap 线程写, 主线程读)
    int deviceIndex = 0;
    int frameMs = 100;
    bool started = false;
    void destroySession();       // closeTap(排干) → 删 tapOb → close+删 player
};

// sherpa 流式识别 (IAudioStt) → partial_result/final_result/stt_ready/error 信号
// 模型加载在后台线程, 不阻塞 Godot 主线程; recognize 由 GDScript 转发 audio_frame。
// recognizeRaw: tap 线程直调 (avox::AvoxData 引用, 不复制), forward_to 模式下使用。
class SttNode : public Node {
    GDCLASS(SttNode, Node)

public:
    SttNode();
    ~SttNode();

    void start();
    void stop();
    bool loading();                       // 模型/任务是否已就绪 (avox loading 语义)
    void recognize(PackedByteArray p_data);
    void setAudioDesc(int p_sample_rate, int p_channels);  // 告知 STT 输入音频格式 (必须, 否则无法识别)
    void setModelLevel(int p_level);       // ModelLevel: 0=none 1=mini 2=base 3=high
    int getModelLevel() const;
    void setRecognizerType(int p_type);    // RecognizerType: 0=none 1=streaming 2=offline
    int getRecognizerType() const;
    avox::IAudioStt *getSttRaw() const;     // 暴露底层指针 (MicCapture forward_to 用, tap 线程直调)

protected:
    static void _bind_methods();
    void _notification(int p_what);

private:
    friend class SttOb;
    std::unique_ptr<avox::IAudioStt> stt;      // 默认 deleter: ~AudioStt 内 stop+releaseEngine
    std::unique_ptr<avox::IAudioSttOb> sttOb;
    int modelLevel = 2;            // ModelLevel::base
    int recognizerType = 1;        // RecognizerType::streaming
    std::atomic<bool> loadStop{false};
    std::thread loadThread;
};

// sherpa 语音合成 (IAudioTts) → tts_audio/tts_ready/tts_desc/error 信号
// 模型加载在后台线程, 不阻塞 Godot 主线程; synthesize 由 GDScript 转发文本。
// 模型对象级常驻: 首轮 start 加载 Kokoro, 后续轮复用 (stop 只停任务不销毁引擎)。
// tts_audio: s16 mono PCM 切片 (PackedByteArray) + pts(ms) + final(本句末片);
// GDScript 用 AudioStream 播放并据 final 分句。
class TtsNode : public Node {
    GDCLASS(TtsNode, Node)

public:
    TtsNode();
    ~TtsNode();

    void start();
    void stop();
    bool loading();                       // 模型/任务是否已就绪 (avox loading 语义 = running)
    void synthesize(const String &p_text);
    void setSpeed(float p_speed);          // 语速倍率 (1.0=正常)
    float getSpeed() const;
    void setSpeaker(int p_sid);            // 多说话人模型选嗓音 (0 起)
    int getSpeaker() const;
    void setModelLevel(int p_level);       // ModelLevel: 0=none 1=mini 2=base 3=high
    int getModelLevel() const;

protected:
    static void _bind_methods();
    void _notification(int p_what);

private:
    friend class TtsOb;
    std::unique_ptr<avox::IAudioTts> tts;       // 默认 deleter: ~AudioTts 内 stop+releaseEngine
    std::unique_ptr<avox::IAudioTtsOb> ttsOb;
    int modelLevel = 2;            // ModelLevel::base
    float speed = 1.0f;
    int sid = 0;
    std::atomic<bool> loadStop{false};
    std::thread loadThread;
};

// 文字注入原语 (SendInput KEYEVENTF_UNICODE): inject_unicode 逐字符打, send_backspace 删
// 不追踪 lastText —— 增量 diff (common prefix + 修正 Backspace) 由 GDScript 算。
class TextInjector : public Node {
    GDCLASS(TextInjector, Node)

public:
    TextInjector() = default;
    ~TextInjector() = default;

    void injectUnicode(const String &p_text);
    void sendBackspace(int p_count);
    bool canInject();
    void setCaptureMode(bool p_on);

protected:
    static void _bind_methods();

private:
    bool captureMode = false;
};

} // namespace godot
