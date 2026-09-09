// voice.cpp — 语音输入原语插件封装 (GlobalHotkey / MicCapture / TextInjector, 纯采集无模型)
// 行为基准: src/avox_cmd/commands/CmdVoice.cpp (CLI 版). 只编译于 Windows.
//
// 线程模型:
//   GlobalHotkey 钩子线程 (PeekMessage 轮询 + quitEvent, 200ms 内可停)
//   MicCapture   tap 线程 (avox 内部) → onFrame 复制成 PackedByteArray
//   两者都经 call_deferred("emit_signal", ...) 投递到 Godot 主线程 (跨线程安全)。

#include "voice.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <avox/AvoxBase.h>

#include <cctype>
#include <chrono>
#include <cstring>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace godot {

// ============================================================
// 公共小工具 (移植自 CmdVoice.cpp)
// ============================================================

namespace {

struct HotkeyCfg {
    int vk = VK_F9;
    bool ctrl = false, shift = false, alt = false;
    int mode = 1;  // 0=hold 1=toggle (存副本, 钩子回调里读)
};

// 热键名 → HotkeyCfg (语法: [ctrl+][alt+][shift+]f9 / right_alt / space / a / F1-F24)
HotkeyCfg parseHotkey(const std::string &s_in) {
    HotkeyCfg cfg;
    std::string s = s_in;
    for (auto &c : s) c = (char)tolower((unsigned char)c);
    if (s.find("ctrl+") != std::string::npos) { cfg.ctrl = true; s.erase(s.find("ctrl+"), 5); }
    if (s.find("alt+") != std::string::npos) { cfg.alt = true; s.erase(s.find("alt+"), 4); }
    if (s.find("shift+") != std::string::npos) { cfg.shift = true; s.erase(s.find("shift+"), 6); }
    if (s == "right_alt" || s == "ralt") cfg.vk = VK_RMENU;
    else if (s == "left_alt" || s == "lalt") cfg.vk = VK_LMENU;
    else if (s == "right_ctrl" || s == "rctrl") cfg.vk = VK_RCONTROL;
    else if (s == "left_ctrl" || s == "lctrl") cfg.vk = VK_LCONTROL;
    else if (s.size() > 1 && s[0] == 'f') {
        int fn = 0;
        for (size_t i = 1; i < s.size(); ++i) {
            if (s[i] < '0' || s[i] > '9') { fn = 0; break; }
            fn = fn * 10 + (s[i] - '0');
        }
        if (fn >= 1 && fn <= 24) cfg.vk = VK_F1 + fn - 1;
    } else if (s == "space") cfg.vk = VK_SPACE;
    else if (s == "tab") cfg.vk = VK_TAB;
    else if (!s.empty() && s.size() == 1 && s[0] >= 'a' && s[0] <= 'z') cfg.vk = 'A' + (s[0] - 'a');
    return cfg;
}

std::string hotkeyDisplayName(const HotkeyCfg &cfg) {
    std::string s;
    if (cfg.ctrl) s += "Ctrl+";
    if (cfg.alt) s += "Alt+";
    if (cfg.shift) s += "Shift+";
    if (cfg.vk == VK_RMENU) s += "Right Alt";
    else if (cfg.vk == VK_LMENU) s += "Left Alt";
    else if (cfg.vk == VK_RCONTROL) s += "Right Ctrl";
    else if (cfg.vk == VK_LCONTROL) s += "Left Ctrl";
    else if (cfg.vk >= VK_F1 && cfg.vk <= VK_F24) s += "F" + std::to_string(cfg.vk - VK_F1 + 1);
    else if (cfg.vk >= 'A' && cfg.vk <= 'Z') s += (char)cfg.vk;
    else s += "Key" + std::to_string(cfg.vk);
    return s;
}

bool checkHotkeyMatch(const KBDLLHOOKSTRUCT &kb, const HotkeyCfg &cfg) {
    if ((int)kb.vkCode != cfg.vk) return false;
    bool altDown = (kb.flags & LLKHF_ALTDOWN) != 0;
    bool ctrlDown = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
    bool shiftDown = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
    bool isAltKey = (cfg.vk == VK_MENU || cfg.vk == VK_LMENU || cfg.vk == VK_RMENU);
    if (!isAltKey && cfg.alt != altDown) return false;
    if (cfg.ctrl != ctrlDown) return false;
    if (cfg.shift != shiftDown) return false;
    return true;
}

// UTF-8 → UTF-16
std::wstring toWstr(const std::string &utf8) {
    if (utf8.empty()) return {};
    int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    if (wlen <= 1) return {};
    std::wstring w((size_t)wlen - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, &w[0], wlen);
    return w;
}

// 宽字面量 → Godot String (UTF-8)。插件编译没有 /utf-8, 窄字面量是 GBK 字节,
// String(...) 按 Latin-1、String::utf8(...) 按 UTF-8 读都乱码; 宽字面量编码无关,
// 是插件内中文文案唯一可靠写法。
String wstrToStr(const wchar_t *ws) {
    if (!ws || !*ws) return String();
    int len = WideCharToMultiByte(CP_UTF8, 0, ws, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 1) return String();
    std::string s((size_t)len - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, ws, -1, &s[0], len, nullptr, nullptr);
    return String::utf8(s.c_str());
}

// 用 Unicode 键盘事件注入一段文字 (每 UTF-16 码元一次 down+up, 代理对自动重组)
void sendUnicodeText(const std::wstring &text) {
    if (text.empty()) return;
    std::vector<INPUT> ins;
    ins.reserve(text.size() * 2);
    for (wchar_t ch : text) {
        INPUT dn{}; dn.type = INPUT_KEYBOARD;
        dn.ki.wScan = ch; dn.ki.dwFlags = KEYEVENTF_UNICODE;
        INPUT up{}; up.type = INPUT_KEYBOARD;
        up.ki.wScan = ch; up.ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
        ins.push_back(dn);
        ins.push_back(up);
    }
    SendInput((UINT)ins.size(), ins.data(), sizeof(INPUT));
}

} // namespace

// ============================================================
// GlobalHotkey
// ============================================================

struct GlobalHotkeyImpl {
    HotkeyCfg toggleCfg;                  // toggle 热键 (mode=1)
    HotkeyCfg holdCfg;                    // hold 热键 (mode=0)
    bool toggleEnabled = false;
    bool holdEnabled = false;
    HHOOK hook = nullptr;
    std::thread hookThread;
    std::atomic<bool> running{false};
    std::atomic<bool> toggleDown{false};  // 抑制自动重复
    std::atomic<bool> holdDown{false};    // 抑制自动重复 + hold 按住状态
    std::atomic<bool> captureMode{false};
    std::atomic<int> activeMode{1};       // 单模式开关: 1=toggle 0=hold (只有该模式热键响应)
    HANDLE quitEvent = nullptr;
    GlobalHotkey *owner = nullptr;
};
// 单实例 (GDScript 实际只建一个); 钩子回调经此访问
static GlobalHotkeyImpl *gHotkeyImpl = nullptr;

// WH_KEYBOARD_LL 回调: 单模式开关 (activeMode) —— 只有当前模式的热键响应, 匹配则全吞
// (down/up 含自动重复)。toggle 模式 → toggled; hold 模式按下 → pressed, 抬起 → released
// (GDScript 以此驱动热键钮的按住高亮)。捕获热键期间 (captureMode) 全部不响应。
LRESULT CALLBACK hotkeyHookProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION && gHotkeyImpl && gHotkeyImpl->running.load() &&
        !gHotkeyImpl->captureMode.load()) {
        KBDLLHOOKSTRUCT *kb = (KBDLLHOOKSTRUCT *)lParam;
        const bool down = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
        const bool up = (wParam == WM_KEYUP || wParam == WM_SYSKEYUP);
        const bool wantToggle = gHotkeyImpl->activeMode.load() == 1;
        bool consumed = false;
        if (wantToggle && gHotkeyImpl->toggleEnabled && checkHotkeyMatch(*kb, gHotkeyImpl->toggleCfg)) {
            if (down && !gHotkeyImpl->toggleDown.exchange(true))
                gHotkeyImpl->owner->call_deferred("emit_signal", "toggled");
            else if (up)
                gHotkeyImpl->toggleDown.store(false);
            consumed = true;
        } else if (!wantToggle && gHotkeyImpl->holdEnabled && checkHotkeyMatch(*kb, gHotkeyImpl->holdCfg)) {
            if (down && !gHotkeyImpl->holdDown.exchange(true))
                gHotkeyImpl->owner->call_deferred("emit_signal", "pressed");
            else if (up) {
                gHotkeyImpl->holdDown.store(false);
                gHotkeyImpl->owner->call_deferred("emit_signal", "released");
            }
            consumed = true;
        }
        if (consumed) return 1;
    }
    return CallNextHookEx(gHotkeyImpl ? gHotkeyImpl->hook : nullptr, nCode, wParam, lParam);
}

GlobalHotkey::GlobalHotkey() = default;
GlobalHotkey::~GlobalHotkey() {
    unregisterHotkey();  // 先 join 钩子线程, 再释放 PIMPL (impl 是 unique_ptr, 自动 delete)
    impl.reset();
}

// 注册一组热键 (kind: "toggle"|"hold"): 停旧钩子 → 更新该组 cfg → 重启钩子。
// 每次都重启保证 cfg 写入与钩子线程读不并发 (设置热键是低频操作, 可接受)。
bool GlobalHotkey::registerHotkey(const String &p_kind, const String &p_name) {
    stopThread();
    if (!impl) impl = std::make_unique<GlobalHotkeyImpl>();
    HotkeyCfg cfg = parseHotkey(p_name.utf8().get_data());
    if (p_kind == "toggle") {
        cfg.mode = 1;
        impl->toggleCfg = cfg;
        impl->toggleEnabled = true;
    } else {
        cfg.mode = 0;
        impl->holdCfg = cfg;
        impl->holdEnabled = true;
    }
    startThread();
    return true;
}

void GlobalHotkey::unregisterHotkey() {
    stopThread();
}

void GlobalHotkey::startThread() {
    impl->owner = this;
    impl->running.store(true);
    impl->toggleDown.store(false);
    impl->holdDown.store(false);
    impl->quitEvent = CreateEventA(nullptr, TRUE, FALSE, nullptr);
    gHotkeyImpl = impl.get();
    impl->hookThread = std::thread([this]() {
        if (impl->running.load()) {
            impl->hook = SetWindowsHookExA(WH_KEYBOARD_LL, hotkeyHookProc,
                                           GetModuleHandleA(nullptr), 0);
        }
        if (!impl->hook && impl->running.load()) {
            impl->running.store(false);
            impl->owner->call_deferred("emit_signal", "error",
                                       wstrToStr(L"注册全局热键失败 (需管理员权限?)"));
        }
        // 阻塞等待 quitEvent 或新消息: WH_KEYBOARD_LL 钩子回调由系统投递到本线程
        // 消息队列, 必须立即泵消息 —— 旧实现 200ms 轮询会让全系统每次按键延迟
        // 最多 200ms (任何位置打字都卡)。MsgWaitForMultipleObjects 对两者都立即唤醒,
        // stopThread 的 SetEvent(quitEvent) 机制不变。
        while (impl->running.load()) {
            DWORD rc = MsgWaitForMultipleObjects(1, &impl->quitEvent, FALSE,
                                                 INFINITE, QS_ALLINPUT);
            if (rc == WAIT_OBJECT_0) break;            // quitEvent signaled → 退出
            if (rc == WAIT_FAILED) break;              // 意外错误, 防死循环
            if (rc == WAIT_OBJECT_0 + 1) {             // 消息到达 → 立即泵
                MSG msg;
                while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
                    TranslateMessage(&msg);
                    DispatchMessageA(&msg);
                    if (msg.message == WM_QUIT) break;
                }
            }
        }
        if (impl->hook) { UnhookWindowsHookEx(impl->hook); impl->hook = nullptr; }
    });
}

void GlobalHotkey::stopThread() {
    if (!impl) return;
    impl->running.store(false);
    if (impl->quitEvent) SetEvent(impl->quitEvent);
    if (impl->hookThread.joinable()) impl->hookThread.join();
    if (impl->quitEvent) { CloseHandle(impl->quitEvent); impl->quitEvent = nullptr; }
    if (gHotkeyImpl == impl.get()) gHotkeyImpl = nullptr;
}

void GlobalHotkey::setCaptureMode(bool p_on) {
    if (impl) impl->captureMode.store(p_on);
}
bool GlobalHotkey::getCaptureMode() const {
    return impl ? impl->captureMode.load() : false;
}

void GlobalHotkey::setActiveMode(const String &p_kind) {
    if (!impl) impl = std::make_unique<GlobalHotkeyImpl>();
    impl->activeMode.store(p_kind == "hold" ? 0 : 1);
    // 换模式时复位按下标志, 防旧模式"残留按下"吞掉新模式下一次按下
    impl->toggleDown.store(false);
    impl->holdDown.store(false);
}
String GlobalHotkey::getActiveMode() const {
    return impl && impl->activeMode.load() == 0 ? "hold" : "toggle";
}

String GlobalHotkey::getHotkeyDisplay(const String &p_kind) const {
    if (!impl) return String::chr(0x2014);  // "—" (编码无关)
    if (p_kind == "toggle")
        return impl->toggleEnabled ? String::utf8(hotkeyDisplayName(impl->toggleCfg).c_str())
                                   : String::chr(0x2014);
    return impl->holdEnabled ? String::utf8(hotkeyDisplayName(impl->holdCfg).c_str())
                             : String::chr(0x2014);
}

void GlobalHotkey::_notification(int p_what) {
    if (p_what == NOTIFICATION_EXIT_TREE) unregisterHotkey();
}

void GlobalHotkey::_bind_methods() {
    ClassDB::bind_method(D_METHOD("register", "kind", "name"), &GlobalHotkey::registerHotkey);
    ClassDB::bind_method(D_METHOD("unregister"), &GlobalHotkey::unregisterHotkey);
    ClassDB::bind_method(D_METHOD("set_capture_mode", "on"), &GlobalHotkey::setCaptureMode);
    ClassDB::bind_method(D_METHOD("get_capture_mode"), &GlobalHotkey::getCaptureMode);
    ClassDB::bind_method(D_METHOD("set_active_mode", "kind"), &GlobalHotkey::setActiveMode);
    ClassDB::bind_method(D_METHOD("get_active_mode"), &GlobalHotkey::getActiveMode);
    ClassDB::bind_method(D_METHOD("get_hotkey_display", "kind"), &GlobalHotkey::getHotkeyDisplay);

    ADD_SIGNAL(MethodInfo("pressed"));
    ADD_SIGNAL(MethodInfo("released"));
    ADD_SIGNAL(MethodInfo("toggled"));
    ADD_SIGNAL(MethodInfo("error", PropertyInfo(Variant::STRING, "msg")));
}

// ============================================================
// MicCapture
// ============================================================

// tap 观察者: 音频描述 + 定长帧 → 信号 (帧复制成 PackedByteArray 后主线程发)
// forward_to 模式: 直接在 tap 线程调 avox IAudioStt::setAudioDesc + recognize (零拷贝, 线程安全)
// 每帧计算 RMS 电平存入 MicCapture.audioLevel (供 UI 轮询)
class MicTapOb : public avox::IAudioTapOb {
public:
    MicCapture *owner = nullptr;
    void onAudioDesc(const avox::AudioDesc &desc) override {
        if (!owner) return;
        // 仍发信号 (GDScript 可用于 UI 显示等)
        owner->call_deferred("emit_signal", "audio_desc", desc.sampleRate, desc.channels);
    }
    void onFrame(const avox::AvoxData &raw, int64_t pts) override {
        if (!owner) return;
        // 计算 RMS 电平 (int16 PCM, 单/双声道)
        if (raw.size > 0 && raw.data) {
            int sampleCount = raw.size / 2;  // int16 = 2 bytes
            const int16_t *samples = reinterpret_cast<const int16_t *>(raw.data);
            double sum = 0.0;
            int n = sampleCount > 480 ? 480 : sampleCount;  // 最多算 480 样本 (10ms@48k)
            for (int i = 0; i < n; ++i) {
                double s = (double)samples[i] / 32768.0;
                sum += s * s;
            }
            float rms = (n > 0) ? sqrtf((float)(sum / n)) : 0.0f;
            owner->audioLevel.store(rms);
        }
        // 回退: 复制成 PackedByteArray 经主线程发
        PackedByteArray pba;
        pba.resize(raw.size);
        if (raw.size > 0 && raw.data) std::memcpy(pba.ptrw(), raw.data, raw.size);
        owner->call_deferred("emit_signal", "audio_frame", pba);
    }
};

MicCapture::MicCapture() = default;

MicCapture::~MicCapture() {
    destroySession();
}

bool MicCapture::start() {
    if (started) return true;
    avox::ADeviceSdk sdk = avox::getDefaltAudioSdk();
    avox::IAudioManager *mgr = avox::getAudioManager(sdk);
    avox::IAudioSource *src = nullptr;
    if (mgr && deviceIndex >= 0 && deviceIndex < mgr->getDeviceCount()) {
        src = mgr->getDevice(deviceIndex);
    }
    if (!src) return false;

    player.reset(avox::createDevicePlayer());
    player->setAudioSource(src);
    avox::IAudioRender *ar = player->getAudioRender();
    tapOb = std::make_unique<MicTapOb>();
    static_cast<MicTapOb *>(tapOb.get())->owner = this;
    if (ar) {
        avox::addAudioTapOb(ar, tapOb.get());
        // AudioDesc{} = 跟随源格式不重采样, frameMs 切片
        ar->openTap(avox::AudioDesc{}, frameMs);
    }
    if (!player->open()) {
        destroySession();
        return false;
    }
    started = true;
    return true;
}

void MicCapture::stop() {
    if (!started) return;
    // 顺序同 CmdVoice destroySession: 先 closeTap(join tap 线程, 排干尾部帧),
    // 再删 tapOb, 最后 close+删 player。GDScript 在 stop 后再翻 recording 标志。
    destroySession();
    started = false;
}

void MicCapture::destroySession() {
    if (!player) return;
    avox::IAudioRender *ar = player->getAudioRender();
    if (ar && tapOb) {
        ar->closeTap();   // join tap 线程, 排干尾部帧 (close 前必须完成)
        avox::removeAudioTapOb(ar, tapOb.get());
    }
    tapOb.reset();        // remove 之后再删观察者
    player.reset();       // deleter: close() + delete
}

PackedStringArray MicCapture::listDevices() const {
    PackedStringArray arr;
    avox::ADeviceSdk sdk = avox::getDefaltAudioSdk();
    avox::IAudioManager *mgr = avox::getAudioManager(sdk);
    if (!mgr) return arr;
    int count = mgr->getDeviceCount();
    for (int i = 0; i < count; ++i) {
        avox::IAudioSource *dev = mgr->getDevice(i);
        if (dev && dev->getDeviceName())
            arr.append(String::utf8(dev->getDeviceName()));   // avox 设备名是 UTF-8, String() 按 Latin-1 读会乱码
        else
            arr.append(String::utf8("Device ") + String::num_int64(i));
    }
    return arr;
}

void MicCapture::setDeviceIndex(int p_idx) { deviceIndex = p_idx; }
int MicCapture::getDeviceIndex() const { return deviceIndex; }
void MicCapture::setFrameMs(int p_ms) { frameMs = p_ms > 0 ? p_ms : 100; }
int MicCapture::getFrameMs() const { return frameMs; }

float MicCapture::getAudioLevel() const {
    return audioLevel.load();
}

void MicCapture::_notification(int p_what) {
    if (p_what == NOTIFICATION_EXIT_TREE) stop();
}

void MicCapture::_bind_methods() {
    ClassDB::bind_method(D_METHOD("start"), &MicCapture::start);
    ClassDB::bind_method(D_METHOD("stop"), &MicCapture::stop);
    ClassDB::bind_method(D_METHOD("list_devices"), &MicCapture::listDevices);
    ClassDB::bind_method(D_METHOD("set_device_index", "index"), &MicCapture::setDeviceIndex);
    ClassDB::bind_method(D_METHOD("get_device_index"), &MicCapture::getDeviceIndex);
    ClassDB::bind_method(D_METHOD("set_frame_ms", "ms"), &MicCapture::setFrameMs);
    ClassDB::bind_method(D_METHOD("get_frame_ms"), &MicCapture::getFrameMs);
    ClassDB::bind_method(D_METHOD("get_audio_level"), &MicCapture::getAudioLevel);

    ADD_PROPERTY(PropertyInfo(Variant::INT, "device_index"), "set_device_index", "get_device_index");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "frame_ms", PROPERTY_HINT_RANGE, "10,500,10"),
                 "set_frame_ms", "get_frame_ms");

    ADD_SIGNAL(MethodInfo("audio_desc",
                          PropertyInfo(Variant::INT, "sample_rate"),
                          PropertyInfo(Variant::INT, "channels")));
    ADD_SIGNAL(MethodInfo("audio_frame", PropertyInfo(Variant::PACKED_BYTE_ARRAY, "data")));
}

// ============================================================
// TextInjector
// ============================================================

void TextInjector::injectUnicode(const String &p_text) {
    std::wstring w = toWstr(p_text.utf8().get_data());
    sendUnicodeText(w);
}

void TextInjector::sendBackspace(int p_count) {
    for (int i = 0; i < p_count; ++i) {
        INPUT in[2];
        ZeroMemory(in, sizeof(in));
        in[0].type = INPUT_KEYBOARD;
        in[0].ki.wVk = VK_BACK;
        in[1].type = INPUT_KEYBOARD;
        in[1].ki.wVk = VK_BACK;
        in[1].ki.dwFlags = KEYEVENTF_KEYUP;
        SendInput(2, in, sizeof(INPUT));
        Sleep(5);
    }
}

bool TextInjector::canInject() {
    // Godot 窗口聚焦时照常注入 (用户可能往自己 App 的输入框打);
    // 仅 KeyCapture 捕获热键期间禁止 (GDScript 设置 capture_mode)
    return !captureMode;
}

void TextInjector::setCaptureMode(bool p_on) {
    captureMode = p_on;
}

void TextInjector::_bind_methods() {
    ClassDB::bind_method(D_METHOD("inject_unicode", "text"), &TextInjector::injectUnicode);
    ClassDB::bind_method(D_METHOD("send_backspace", "count"), &TextInjector::sendBackspace);
    ClassDB::bind_method(D_METHOD("can_inject"), &TextInjector::canInject);
    ClassDB::bind_method(D_METHOD("set_capture_mode", "on"), &TextInjector::setCaptureMode);
}

} // namespace godot
