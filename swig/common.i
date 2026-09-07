// directors = "1" 开启代理类,使之能继承回调类
%module(directors = "1") AvoxWrapper

%feature("director") ILogOb;
%feature("director") IVInputLayerOb;
%feature("director") IVOutputLayerOb;
%feature("director") IMediaPlayerOb;
%feature("director") IVideoSourceOb;
%feature("director") IAudioSourceOb;
%feature("director") IAVSourceOb;
%feature("director") ISdpAgentOb;
// webrtc v2: 信令通道(引擎侧实现)与rtc扩展回调
%feature("director") ISignalChannel;
%feature("director") IRtcEventOb;
%feature("director") IAudioSttOb;
%feature("director") IAudioTtsOb;
%feature("director") IAudioFaceOb;
%feature("director") IRecorderOb;
%feature("director") ISurfaceRenderOb;
%feature("director") IAudioTapOb;
// avox_agent
%feature("director") ISessionObserver;
%feature("director") IApprovalUi;

%{
// #ifdef SWIGJAVASCRIPT
// #include <node.h> 
// #endif   
// 如果生成的类里的结构不在这些头文件里，就会找不到引用
#include "avox/AvoxDef.h"
#include "avox/AvoxBase.h"
#include "avox/AvoxImage.h"
#include "avox/AvoxVideo.h"
#include "avox/AvoxAudio.h"
#include "avox/AvoxLayer.h"
#include "avox/AvoxCodec.h"
#include "avox/AvoxSource.h"
#include "avox/AvoxMuxer.h"
#include "avox/AvoxPlayer.h"
#include "avox/AvoxInput.h"
#include "avox/AvoxVision.h"
#include "avox_cmd/CmdExecute.h"
#include "avox/AvoxMath.h"
#include "avox/AvoxCore.h"
#include "avox_agent/AgentExport.h"
#include "avox_vulkan/VkExport.h"
// avox_webrtc 已迁 plugins/avox_webrtc; IRtcPlayer/RtcRollType/ISdpAgentOb/createWebRtcPlayer/createZlTestSdpAgent
// addRtcPlayerOb/removeRtcPlayerOb 均由 %include "avox/AvoxPlayer.h" 绑定(AVOX_EXPORT, 随 avox.dll)
// addRtcPlayerOb/removeRtcPlayerOb 通过 dynamic_cast<BasePlayer*> cross-cast 实现, 核心层无需知道 RtcPlayer
#include "avox_freetype/FreetypeExport.h"
#include "avox_zlmediakit/ZlmExport.h"
#ifdef _WIN32
#include "avox_windows/WinExport.h"
#endif 
#ifdef __ANDROID__
#include "avox_android/AndHelper.h"
#endif
#include "avox_egl/EglExport.h"
// 添加JsMediaPlayerOb相关的头文件包含
#ifdef SWIGJAVASCRIPT
#include "nodejs/nativeOb/AvoxJsOb.h"
#endif
%}

// 忽略这些函数，因为会导致swig出错
#ifndef SWIGJAVASCRIPT
%ignore avox::logAction;
%ignore avox::setLogAction;
#endif
%ignore YUVFrame::data;
%ignore YUVFrame::stride;
%ignore operator==;
%ignore operator!=;
%ignore operator[];
%ignore operator*;
%ignore operator+;
%ignore operator-;
%ignore operator();
%ignore operator=;
// cmdExecute 的 int32_t argc + const char* const* argv 形态不面向 Python (无 typemap);
// Python 用 cmdExecuteLine(单行字符串)/cmdExecuteLastExitCode() 这两个便利入口。
%ignore cmdExecute;

#ifdef SWIGJAVASCRIPT
// JS 绑定屏蔽 VkDevice 间句柄直传: VkSharedHandle(uint64_t 成员) 触发 SWIG 4.3.1 napi
// typemap 对 unsigned long long 的 bug(obj.ToNumber().Int64Value()), 在
// NODE_ADDON_API_DISABLE_CPP_EXCEPTIONS 下编译失败(C2039)。JS 用不上 Vulkan 句柄直传;
// C#/Java/Python 不受影响(本块仅 SWIGJAVASCRIPT 生效)。
%ignore avox::VkSharedHandle;
%ignore avox::getVkOutputHandle;
%ignore avox::setVkInputHandle;
#endif

// 所有 create* 返回新对象, 目标语言 GC 释放; 不再配对 destroy* (原 destroyScreenCapture 已删)
%newobject createJsonOption;
%newobject createMediaPlayer;
%newobject createDevicePlayer;
%newobject createWebRtcPlayer;
%newobject createZlTestSdpAgent;
%newobject createImageBuffer;
%newobject toGrayImage;
%newobject createInputController;
%newobject createScreenCapture;
%newobject createTextRecognizer;
%newobject createTemplateMatcher;
%newobject createFeatureMatcher;
%newobject createColorDetector;
%newobject createOrientationDetector;
%newobject createMapMatcher;
%newobject createMaskBuilder;
// IMaskBuilder 的 build*/mask* 返回新 IImageBuffer, 目标语言 GC 释放
%newobject IMaskBuilder::buildSectorMask;
%newobject IMaskBuilder::buildCircleMask;
%newobject IMaskBuilder::buildColorRangeMask;
%newobject IMaskBuilder::maskAnd;
%newobject IMaskBuilder::maskOr;
%newobject IMaskBuilder::maskNot;
%newobject createYoloDetector;
%newobject createImageRender;
%newobject createRecorder;
%newobject createGLESContext;
// agent 宿主 (新架构): 目标语言 GC 释放。
//
// openAgent 返回的 IAgentSession 是**借用指针** (由 host 拥有), 所以不标 %newobject ——
// agent 的生命周期必须嵌套在 host 内, GC 顺序不确定会导致 use-after-free。
%newobject createAgentHost;
%newobject createWavSave;
%newobject createAudioStt;
%newobject createAudioTts;
%newobject createAudioFace;
%newobject createTranslator;

// 没有的话,int32_t对应不了int, uint8_t对应不了IntPtr
%include "stdint.i"

#ifdef SWIGCSHARP
// 将C++ 中 void*/uint8_t*转C# IntPtr
%apply void *VOID_INT_PTR { void *,uint8_t *,unsigned char * }

%typemap(csvarout, excode=SWIGEXCODE2) void* %{
    get {
        global::System.IntPtr ret = $imcall;
        return ret;
    }
%}
#endif

#ifdef SWIGJAVASCRIPT
%{
#include <napi.h>
#include <windows.h>
%}
%typemap(in) Napi::Value {
  $1 = $input; // $input 是 SWIG 包装层传入的 Napi::Value
}
// 通用显式析构重native对象: %newobject只挂GC finalizer, 时机不可控, 高频开关下内存滞留可达GB
// RELEASE(DISOWN|CLEAR)摘所有权后delete(无双释放); 新增重类型在类型表加分支, 需有虚析构
%native(destroyObject) Napi::Value destroyObject(const Napi::CallbackInfo &info);
%wrapper %{
Napi::Value destroyObject(const Napi::CallbackInfo &info) {
  void *argp = nullptr;
  if (info.Length() >= 1) {
    // 类型表: ConvertPtr 先做类型匹配, 匹配失败不触碰所有权, 可安全链式尝试
    if (SWIG_ConvertPtr(info[0], &argp, SWIGTYPE_p_avox__IMediaPlayer, SWIG_POINTER_RELEASE) == SWIG_OK && argp) {
      delete static_cast<avox::IMediaPlayer *>(argp);
    } else if (SWIG_ConvertPtr(info[0], &argp, SWIGTYPE_p_avox__ISourcePlayer, SWIG_POINTER_RELEASE) == SWIG_OK && argp) {
      delete static_cast<avox::ISourcePlayer *>(argp);
    } else if (SWIG_ConvertPtr(info[0], &argp, SWIGTYPE_p_avox__IRtcPlayer, SWIG_POINTER_RELEASE) == SWIG_OK && argp) {
      delete static_cast<avox::IRtcPlayer *>(argp);
    } else if (SWIG_ConvertPtr(info[0], &argp, SWIGTYPE_p_avox__IRecorder, SWIG_POINTER_RELEASE) == SWIG_OK && argp) {
      delete static_cast<avox::IRecorder *>(argp);
    } else if (SWIG_ConvertPtr(info[0], &argp, SWIGTYPE_p_avox__IScreenCapture, SWIG_POINTER_RELEASE) == SWIG_OK && argp) {
      delete static_cast<avox::IScreenCapture *>(argp);
    }
  }
  return info.Env().Undefined();
}
%}
%typemap(out) void* getRenderSharedHandle {
    HANDLE h = (HANDLE)$1;
    if (h == NULL) {
        $result = env.Null();
    } else {
        $result = Napi::Buffer<HANDLE>::Copy(env, &h, 1);
    }
}
#endif

#ifdef SWIGJAVA
// https://forge.naos-cluster.tech/aquinetic/f2i-consulting/fesapi/-/blob/c0a52292680e4ec316d2e3447b52f365a54cc400/cmake/swigModule.i
// getCPtr由protected改成public
SWIG_JAVABODY_PROXY(protected, public, SWIGTYPE)
SWIG_JAVABODY_TYPEWRAPPER(public, public, public, SWIGTYPE)
%pragma(java) jniclasscode=%{
  static {
    try {
        System.loadLibrary("avox");
    } catch (UnsatisfiedLinkError e) {
      System.err.println("avox native code library failed to load. \n" + e);
      System.exit(1);
    }
  }
%}
// %apply long long { void *liveContext }
#endif

// %include "arrays_csharp.i"
%include "carrays.i" 
%include "windows.i" 
//如下文件生成包装
%include "avox/AvoxDef.h"
%include "avox/AvoxBuffer.h"
%include "avox/AvoxLog.h"
%include "avox/AvoxTime.h"
%include "avox/AvoxBase.h"
%include "avox/AvoxMath.h"
%include "avox/AvoxImage.h"
%include "avox/AvoxVideo.h"
%include "avox/AvoxAudio.h"
%include "avox/AvoxLayer.h"
%include "avox/AvoxCodec.h"
%include "avox/AvoxSource.h"
%include "avox/AvoxMuxer.h"
%include "avox/AvoxPlayer.h"
%include "avox/AvoxInput.h"
%include "avox/AvoxVision.h"
%include "avox_cmd/CmdExecute.h"
// avox_agent 导出层: createAgentHost / runSkill / agentShellRun (CLI 用) / lastAgentHostError。
// openAgent 返回**借用指针** (host 拥有), 不标 %newobject; 会话生命周期嵌套在 host 内。
// SWIG 生成后, JS 端在 window.avox 上拿到 createAgentHost / runSkill / agentShellRun 三个
// 函数以及 IAgentHost / IAgentSession / ISessionObserver / IApprovalUi 四个 C++ 类的镜像。
// 临时: agent 模块未编入当前 avox.dll(依赖缺失), 注释以让 SWIG 链接通过; 验证后还原
%include "avox_agent/AgentExport.h"
#ifdef AVOX_ENABLE_VULKAN
%include "avox_vulkan/VkExport.h"
#endif
#ifdef AVOX_ENABLE_ZLMEDIAKIT
%include "avox_zlmediakit/ZlmExport.h"
#endif
#ifdef AVOX_ENABLE_FREETYPE
%include "avox_freetype/FreetypeExport.h"
#endif
#ifdef SWIGJAVASCRIPT
%include "nodejs/nativeOb/AvoxJsOb.h"
#endif
// AVOX_WIN32
#ifndef AVOX_ANDROID 
%include "avox_windows/WinExport.h"
#endif
#ifdef AVOX_ANDROID
%include "avox_android/AndHelper.h"
%include "avox_egl/EglExport.h" 
#endif

%nodefaultctor;
%nodefaultdtor;

%clearnodefaultctor;
%clearnodefaultdtor;
