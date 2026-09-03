#include "audio.h"

//音频桥接实现占位
//当前方案: avox 自带 wasapi 音频渲染, 无需桥接
//后续如需在 Godot 内控制音频输出, 可通过 IAudioTapOb 拦截 PCM 数据
//喂入 Godot 的 AudioStreamPlayback
