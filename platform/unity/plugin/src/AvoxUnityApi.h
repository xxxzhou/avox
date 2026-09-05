#pragma once

#include <stdint.h>

// avox_unity C API — Unity C# P/Invoke 入口 (cdecl, extern "C")
// 见 platform/unity/plugin/unity/com.avox.player/Runtime/AvoxNative.cs

#define AVOX_UNITY_API extern "C" __declspec(dllexport)

typedef void* avox_player_t;  // PlayerBridge*, 仅作不透明句柄

// ── 全局 ──

// CPU 路径纹理更新回调 (IssuePluginCustomTextureUpdateV2 用)
AVOX_UNITY_API void* avoxGetTextureUpdateCallback(void);
// D3D11 拷贝模式渲染事件函数 (GL.IssuePluginEvent 用, eventId = player id)
AVOX_UNITY_API void* avoxGetRenderEventFunc(void);
// GPU 直通是否可用 (Unity Vulkan 后端 + volk 就绪)
AVOX_UNITY_API int32_t avoxGetGpuPassthroughAvailable(void);
// 0 无 1 Vulkan 导入 2 D3D11 拷贝
AVOX_UNITY_API int32_t avoxGetGpuFlavor(void);
// SDK 版本串 (静态缓冲)
AVOX_UNITY_API const char* avoxGetVersion(void);

// ── 播放器生命周期 ──

AVOX_UNITY_API avox_player_t avoxPlayerCreate(void);
AVOX_UNITY_API void avoxPlayerDestroy(avox_player_t player);
// 实例 id (纹理更新回调 userData 用)
AVOX_UNITY_API uint32_t avoxPlayerGetId(avox_player_t player);

// ── 配置 (open 前调用) ──

AVOX_UNITY_API void avoxPlayerSetHardDecode(avox_player_t player, int32_t bEnable);
AVOX_UNITY_API void avoxPlayerSetVolume(avox_player_t player, float volume);
// 0=none 1=zlmediakit 2=ffmpeg 3=torrent
AVOX_UNITY_API void avoxPlayerSetIoPlan(avox_player_t player, int32_t plan);
AVOX_UNITY_API void avoxPlayerSetSpeed(avox_player_t player, double speed);

// ── 控制 ──

AVOX_UNITY_API void avoxPlayerOpen(avox_player_t player, const char* url);
AVOX_UNITY_API void avoxPlayerClose(avox_player_t player);
AVOX_UNITY_API void avoxPlayerPause(avox_player_t player);
AVOX_UNITY_API void avoxPlayerResume(avox_player_t player);
AVOX_UNITY_API void avoxPlayerSeek(avox_player_t player, int64_t pos);

// ── 状态/信息 ──

// avox::PlayerState 数值: 0 none 1 opening 2 ready 3 playing 4 pause 5 seek 6 buffering 7 stopped 8 completed
AVOX_UNITY_API int32_t avoxPlayerGetState(avox_player_t player);
AVOX_UNITY_API int64_t avoxPlayerGetDuration(avox_player_t player);
AVOX_UNITY_API int64_t avoxPlayerGetPosition(avox_player_t player);
AVOX_UNITY_API double avoxPlayerGetProgress(avox_player_t player);
// 弹出一条事件, 返回 0=无 1=有 (out 布局见 AvoxUnityEvent)
AVOX_UNITY_API int32_t avoxPlayerPollEvent(avox_player_t player, void* out /*AvoxUnityEvent*/);
// 帧尺寸 (GPU=导入纹理, CPU=BGRA槽), 返回 0=无帧 1=有
AVOX_UNITY_API int32_t avoxPlayerGetFrameInfo(avox_player_t player, int32_t* w, int32_t* h);
// 是否 GPU 直通模式
AVOX_UNITY_API int32_t avoxPlayerIsGpuMode(avox_player_t player);
// GPU 导入的 VkImage (Unity Vulkan 后端 Texture2D.CreateExternalTexture 用), 0=未就绪
AVOX_UNITY_API uint64_t avoxPlayerGetExternalTexture(avox_player_t player);
// GPU 直通主线程处理 (Unity Update 调): volk 初始化 + enableVkOutput + NT句柄导入 + 尺寸重导
AVOX_UNITY_API void avoxPlayerUpdateGpu(avox_player_t player);
// D3D11 拷贝模式: 设置拷贝目标 (C# 普通 Texture2D 的 GetNativeTexturePtr, BGRA32;
// null 清除)。渲染线程每帧把 avox 底层共享纹理 CopyResource 到它上面
AVOX_UNITY_API void avoxPlayerSetDx11Target(avox_player_t player, void* nativeTex);
// 拷贝链路诊断: 渲染事件数/实际拷贝数/目标缺失数/最近 fence 值/共享纹理打开次数
AVOX_UNITY_API void avoxPlayerGetDx11Debug(avox_player_t player, int32_t* events,
                                           int32_t* copies, int32_t* targetNull,
                                           int64_t* fenceVal, int32_t* opens);

// ── Option (键值参数, 透传 IMediaPlayer::getOption; open 前后均可) ──

// avox::ArgType 数值: 0 null 1 bool 2 int 3 number 4 string
AVOX_UNITY_API int32_t avoxPlayerGetOptionType(avox_player_t player, const char* key);
AVOX_UNITY_API int32_t avoxPlayerSetOptionBool(avox_player_t player, const char* key, int32_t value);
AVOX_UNITY_API int32_t avoxPlayerSetOptionInt(avox_player_t player, const char* key, int64_t value);
AVOX_UNITY_API int32_t avoxPlayerSetOptionNumber(avox_player_t player, const char* key, double value);
AVOX_UNITY_API int32_t avoxPlayerSetOptionString(avox_player_t player, const char* key, const char* value);
AVOX_UNITY_API int64_t avoxPlayerGetOptionInt(avox_player_t player, const char* key);
AVOX_UNITY_API double avoxPlayerGetOptionNumber(avox_player_t player, const char* key);
// 写入 buf(含\0, 不足截断), 返回长度; 键不存在/类型不符返回 -1
AVOX_UNITY_API int32_t avoxPlayerGetOptionString(avox_player_t player, const char* key, char* buf, int32_t bufSize);

// ── 录制 (ffmpeg 封装到本地文件/推流地址) ──

// 返回 1=已打开开始录制
AVOX_UNITY_API int32_t avoxPlayerStartRecord(avox_player_t player, const char* path, int32_t bTranscode);
AVOX_UNITY_API void avoxPlayerStopRecord(avox_player_t player);
// avox::RecorderState 数值: 0 none 1 opening 2 recording 3 completed
AVOX_UNITY_API int32_t avoxPlayerGetRecordState(avox_player_t player);

// ── 字幕 ──

// 加载 SRT 字幕文件, 返回 1=成功 (ASR/翻译依赖可选模块, 未集成时返回 0)
AVOX_UNITY_API int32_t avoxPlayerLoadSrt(avox_player_t player, const char* path);
AVOX_UNITY_API void avoxPlayerCloseSubtitle(avox_player_t player);
