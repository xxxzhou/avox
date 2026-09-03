#pragma once

#include <avox/AvoxAudio.h>

namespace godot {

/// avox 音频 → Godot AudioStream 桥接 (占位, 后续实现)
/// 方案1: avox 自带音频渲染 (wasapi), Godot 不参与
/// 方案2: 通过 IAudioTapOb 拦截 PCM, 喂给 Godot AudioStream
class AudioBridge {
public:
    AudioBridge() = default;
    ~AudioBridge() = default;
};

} // namespace godot
