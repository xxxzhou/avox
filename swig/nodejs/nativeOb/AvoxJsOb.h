#pragma once

#include <napi.h>
#include <node.h>

#include "../../../src/avox/AvoxLog.h"
#include "../../../src/avox/AvoxPlayer.h"
#include "../../../src/avox_agent/AgentExport.h"
// avox_webrtc 已迁 plugins/avox_webrtc; IRtcPlayer/IRtcEventOb/createWebRtcPlayer/createZlTestSdpAgent
// addRtcPlayerOb/removeRtcPlayerOb 均由 avox/AvoxPlayer.h 导出(AVOX_EXPORT), 已由上方 #include 覆盖

namespace avox {

extern "C" {
// JS 侧只需传入一个 observer 对象
IMediaPlayerOb* createJsMediaPlayerOb(Napi::Value observer);
IRtcEventOb* createJsRtcEventOb(Napi::Value observer);
ILogOb* createJsLogOb(Napi::Value observer);
ISurfaceRenderOb* createJsSurfaceRenderOb(Napi::Value observer);
IRecorderOb* createJsRecorderOb(Napi::Value observer);
// avox_agent 回调包装: observer 是 JS 侧的对象, 实现 onSessionEvent / onToken /
// onToolCall / ask 等方法。JS 对象的常见方法名见各 cpp 里的 queueCallback 调用。
ISessionObserver* createJsSessionObserverOb(Napi::Value observer);
IApprovalUi* createJsApprovalUiOb(Napi::Value observer);
// 设置ISurfaceRender的输出JS Buffer对象
void setRenderJsBuffer(ISurfaceRenderOb* winOb, Napi::Value buffer);
// 渲染EglTexture到JS Buffer上
int32_t renderEglResource(ISurfaceRender* render);
}

}