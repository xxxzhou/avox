#pragma once
#include <functional>
#include <map>
#include <vector>

#include "../Avox.hpp"   // AvoxCore.h 聚合头 + AndroidEnv (原在 AvoxCore.h, 内部用不导出)
#include "../audio/AudioDecoder.hpp"
#include "../audio/AudioEncoder.hpp"
#include "../audio/AudioStt.hpp"
#include "../audio/AudioTts.hpp"
#include "../audio/AudioFace.hpp"
#include "../avatar/VideoFace.hpp"
#include "../avatar/BodyImpl.hpp"
#include "../audio/AudioProcess.hpp"
#include "../audio/AudioRender.hpp"
#include "../AvoxPlayer.h"
#include "../muxer/IOMuxer.hpp"
#include "../AvoxVision.h"
#include "../AvoxCalib.h"
#include "../AvoxScene.h"
#include "../vision/IONNXSession.hpp"
#include "../vision/IOVEngine.hpp"
#include "../vision/OnnxSessionCache.hpp"
#include "../video/IImageProc.hpp"
#include "../subtitle/BaseTranslator.hpp"
#include "../source/AVSource.hpp"
#include "../source/DeviceManager.hpp"
#include "../source/RawSource.hpp"
#include "../video/VideoDecoder.hpp"
#include "../video/VideoEncoder.hpp"
#include "../video/VideoRender.hpp"
#include "RegeditObj.hpp"

#ifdef __ANDROID__
#include <android/asset_manager_jni.h>
#include <android/native_activity.h>
#include <android_native_app_glue.h>
struct android_app;
#endif

namespace avox {

// 管理工厂类，如解码器工厂注册到这，从这可以看到所有解码器工厂
// 同时不同平台初始化，后期看有没必要分开
class AVOX_EXPORT AvoxManager {
 public:
  static AvoxManager& Get();
  // 清理资源
  static void clean();

 private:
  AvoxManager(/* args */);
  static AvoxManager* instance;
  AvoxManager(const AvoxManager&) = delete;
  // 防止别人误写 auto inst = AvoxManager::Get(),必需引用
  AvoxManager& operator=(const AvoxManager&) = delete;

 public:
  ~AvoxManager();
  // 在模块初始化前调用
  void init();
  // ASR/ONNX/翻译/水印去除/WebRTC/音频处理 工厂注册(插件 loadModule 时调);
  // 业务通过 xxxFactory.create("name") 查表拿实例, 解除核心对插件的编译期 include 依赖
  // (组件可用才注册, 查不到返回 nullptr 降级)
  // 工厂表用 C 函数指针(非 std::function): std::function 的 type-erasure manager 实例化在
  // plugin 的 reg 调用点(plugin dll), 进程退出 AvoxManager 析构晚于 plugin 卸载 -> 调已卸载 manager 崩溃。
  // 裸函数指针析构不调代码, 无此患; 无捕获 lambda 隐式转换。
  RegeditFactory<AudioStt> audioSttHub;
  // TTS 工厂 (avox_sherpa loadModule 时 reg "sherpa"; 文本→PCM 合成, 与 audioSttHub 对称)
  RegeditFactory<AudioTts> audioTtsHub;
  RegeditFactory<IONNXSession> onnxSessionHub;
  // OpenVINO 推理引擎工厂 (VkQEnhanceLayer 用; Intel iGPU/CPU)。
  // plugins/avox_openvino loadModule 时 reg "openvino"; plugin 没装时 create 返回 nullptr 降级 ORT。
  RegeditFactory<IOVEngine> openvinoEngineHub;
  // 共享 ONNX session 缓存: 模型全局加载一次、对象复用 (经 OnnxModelUser 使用)。
  // 运行期手动 unload; 退出不卸载 (跨 DLL 不安全), 随进程泄漏。详见 vision/OnnxSessionCache.hpp。
  OnnxSessionCache onnxSessionCache;
  RegeditFactory<BaseTranslator> translatorHub;
  RegeditFactory<IWatermarkRemoval> watermarkRemovalHub;
  RegeditFactory<ITemplateMatcher> templateMatcherHub;
  RegeditFactory<ITextRecognizer> textRecognizerHub;
  // 特征匹配/颜色区域/方向检测 工厂 (avox_opencv loadModule 时 reg "opencv");
  // 业务经 xxxHub.create("opencv") 取实例, plugin 没装时返回 nullptr 降级。
  RegeditFactory<IFeatureMatcher> featureMatcherHub;
  RegeditFactory<IColorDetector> colorDetectorHub;
  RegeditFactory<IOrientationDetector> orientationDetectorHub;
  // 地图定位工厂 (avox_opencv loadModule 时 reg "opencv"; BGI 模板匹配方案)
  RegeditFactory<IMapMatcher> mapMatcherHub;
  // 通用掩码生成工厂 (avox_opencv loadModule 时 reg "opencv"; 几何/颜色/位运算算子)
  RegeditFactory<IMaskBuilder> maskBuilderHub;
  // 图像处理工厂 (avox_opencv loadModule 时 reg "opencv"; load/resize 等 cv 能力);
  // ImageIO 经 imageProcHub.create("opencv") 取实例, plugin 没装时返回 nullptr 降级(stb+手写)。
  RegeditFactory<IImageProc> imageProcHub;
  // 通用 YOLO 工厂 (avox_cv loadModule 时 reg "yolo"; 标准 Ultralytics 检测/分类)
  RegeditFactory<IYoloDetector> yoloDetectorHub;
  RegeditFactory<IRtcPlayer> rtcPlayerHub;
  RegeditFactory<AudioProcess> audioProcessHub;
  // 音频→blendshape 工厂 (avox_avatar loadModule 时 reg "wav2arkit"; PCM→ARKit52, 驱动虚拟人口型)。
  // 必须追加到末尾: 新增 hub 插在中间会改变其后所有成员偏移, 未重编的插件(avox_onnx/avox_sherpa/...)
  // 二进制仍按旧偏移访问 onnxSessionHub 等 → loadModule 写错地址崩。追加末尾则旧成员偏移不变, 增量构建安全。
  RegeditFactory<AudioFace> audioFaceHub;
  // 视频→blendshape+landmarks 工厂 (avox_avatar loadModule 时 reg "mediapipe"; RGBA→ARKit52+478点, 视频驱动虚拟人)。
  // 同样必须末尾追加 (见上 audioFaceHub 注: 插中间改偏移致未重编插件崩)。
  RegeditFactory<VideoFace> videoFaceHub;
  // 视频→身体姿态 33 点工厂 (avox_avatar loadModule 时 reg "mediapipe_body"; RGBA→33点身体 landmark, 驱动 avatar 全身骨骼/手势)。
  // 末尾追加 (同上 ABI 约束)。
  RegeditFactory<BodyImpl> bodyHub;

 public:
  // 需要在加载库时自动调用的方法
  std::vector<RegFunc> initFuncs;
  // 模块清理函数(与 initFuncs 配对, clean() 里逆序调用), 与 init 对称
  std::vector<RegFunc> cleanFuncs;
  // IO处理对象注册
  RegeditObj<IoPlan, AVSource, IoPlanDesc> ioSources;
  // 源对象注册
  RegeditObj<RawSourceType, RawSource, RawSourceDesc> rawSources;
  // 媒体复用器注册
  RegeditObj<MuxerType, IOMuxer, MuxerDesc> muxers;
  // 视频渲染注册
  RegeditObj<RenderType, VideoRender, VRenderDesc> vRender;
  // 音频渲染注册
  RegeditObj<ARenderType, AudioRender, ARenderDesc> aRender;
  // 音频解码器注册
  RegeditObjList<ACodecId, AudioDecoder, ACodecDesc> aDecoders;
  // 视频解码器注册
  RegeditObjList<VCodecId, VideoDecoder, VCodecDesc> vDecoders;
  // 音频编码器注册
  RegeditObjList<ACodecId, AudioEncoder, ACodecDesc> aEncoders;
  // 视频编码器注册
  RegeditObjList<VCodecId, VideoEncoder, VCodecDesc> vEncoders;
  // 音频设备管理类注册
  RegeditMgr<ADeviceSdk, IAudioManager> aDeviceMgr;
  // 视频设备管理类注册
  RegeditMgr<VDeviceSdk, IVideoManager> vDeviceMgr;
  // 数据源探测工厂 (avox_torrent loadModule 时 reg "torrent"; 磁力/BT 文件列表+选文件,
  // 接口 ISourceProbe 见 AvoxBase.h, createSourceProbe 出口)。
  // 必须追加在全部成员末尾 (ABI 约束同上 audioFaceHub 注: 插在中间会挪动其后
  // 所有成员偏移, 未重编插件按旧偏移访问 ioSources/vRender 等即写错地址崩)。
  RegeditFactory<ISourceProbe> sourceProbeHub;
  // ============ 虚拟制片标定工厂 (avox_calib loadModule 时 reg "opencv") ============
  // 内参/手眼+scale/PnP/序列标定, 接口见 AvoxCalib.h, 移植自 aoce 虚拟制片标定方案
  // (doc/plan/虚拟制片标定移植方案.md)。同样必须末尾追加 (ABI 约束同上)。
  RegeditFactory<IImagePoints> imagePointsHub;
  RegeditFactory<ICameraCalibration> cameraCalibrationHub;
  RegeditFactory<ICameraOffset> cameraOffsetHub;
  RegeditFactory<IPnpCameraPose> pnpCameraPoseHub;
  RegeditFactory<IVideoCalibration> videoCalibrationHub;
  RegeditFactory<ILedMeshBuild> ledMeshBuildHub;
  // FBX 场景导入工厂 (avox_fbx loadModule 时 reg "fbx"; 接口见 AvoxScene.h)
  // 同样必须末尾追加 (ABI 约束同上)。
  RegeditFactory<ISceneImport> sceneImportHub;

 private:
  bool bInit = false;
  // 切换到后台
  bool background = false;

 public:
  void enterBack(bool back);
  // 是否在后台
  bool getBackground();

#ifdef __ANDROID__
 private:
  android_app* app = nullptr;
  AndroidEnv androidEnv = {};
  bool bAttach = false;

 public:
  // 如果是要用nativewindow,请使用这个,里面有active/assetmanager等
  void initAndroid(android_app* app);
  inline android_app* getApp() { return app; }
  // 如果不是nativewindow,请尽量填充AndroidEnv里的JavaVM等相关
  void initAndroid(const AndroidEnv& andEnv);
  const AndroidEnv& getAppEnv() const { return androidEnv; }
  AndroidEnv& getAppEnv() { return androidEnv; }
  // 要使用jni里的如findcalss/GetStaticMethodID等方法
  // 必需在主线程或是附加主线程里调用
  JNIEnv* getEnv(bool* bAttach = nullptr);
  // initAndroid里保存了一个env,如果在initAndroid线程里,不需要传入env
  jobject getActivityApplication(jobject activity, JNIEnv* env = nullptr);
  std::string getObjClassName(jobject obj, JNIEnv* env = nullptr);

  // 如果在线程拿过JNIEnv,退出时请调用
  void detachThread();
#endif
};

}
