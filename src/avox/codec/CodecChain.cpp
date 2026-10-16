#include "CodecChain.hpp"

namespace avox {

// 平台默认解码器名: 候选排序的**优先级提示**。注册表给不出"谁更好", 本表补这一层
// 已知信息(哪个名字是本平台的成熟车道)。注意各分支都必须给出该编码真实存在的
// 名字: 提示名在注册表里找不到就不移动任何候选(无害), 但末尾的 H264 兜底对
// 未列编码是无意义提示 —— 它不再是"选型失败后的落点", 老的隐式兜底已随候选表
// 改成注册表全量而消失。
const char* getDefaultDecoderName(VCodecId codecId, bool bHard) {
  if (codecId == VCodecId::h264) {
// 注意不同编码器，sps/vps解码有些可以，有些不行
#ifdef __ANDROID__
    // AVOX_ANDROID_H264_DECODER h264
    return bHard ? AVOX_ANDROID_H264_DECODER : AVOX_FF_H264_DECODER;
#elif __APPLE__
    // h264_qsv h264_vulkan h264 libx264
    return bHard ? AVOX_IOS_H264_DECODER : AVOX_FF_H264_DECODER;
#elif defined(__ONLY_LINUX__)
    // Linux 硬解主路 VAAPI(此前误映射 dx11 名字恒落软解), 失败回退 vulkan → 软解
    return bHard ? AVOX_FFVAAPI_H264_DECODER : AVOX_FF_H264_DECODER;
#else
    // h264_qsv h264_vulkan h264 libx264
    return bHard ? AVOX_FFDX11_H264_DECODER : AVOX_FF_H264_DECODER;
#endif
  } else if (codecId == VCodecId::h265) {
    // hevc_qsv hevc_vulkan hevc libx265
#ifdef __ANDROID__
    // AVOX_ANDROID_H264_DECODER hevc
    return bHard ? AVOX_ANDROID_H265_DECODER : AVOX_FF_H265_DECODER;
#elif __APPLE__
    // hevc_qsv hevc_vulkan hevc libx264
    return bHard ? AVOX_IOS_H265_DECODER : AVOX_FF_H265_DECODER;
#elif defined(__ONLY_LINUX__)
    // Linux 硬解主路 VAAPI, 失败回退 vulkan → 软解
    return bHard ? AVOX_FFVAAPI_H265_DECODER : AVOX_FF_H265_DECODER;
#else
    // hevc_qsv hevc_vulkan hevc libx265 AVOX_FFVULKAN_H265_DECODER
    // AVOX_FFDX11_H265_DECODER
    return bHard ? AVOX_FFDX11_H265_DECODER : AVOX_FF_H265_DECODER;
#endif
  } else if (codecId == VCodecId::vp9) {
#if defined(_WIN32)
    // webm/VP9: Windows 走 D3D11VA(FFDx11Decoder::onVaild 探测 GPU 解码 profile,
    // 不支持时由选型链回退下一条候选)
    return bHard ? AVOX_FFDX11_VP9_DECODER : AVOX_FF_VP9_DECODER;
#elif defined(__ANDROID__)
    // Android 走 MediaCodec(AndVDecoder::onVaild 按解码器名排除平台软实现)
    return bHard ? AVOX_ANDROID_VP9_DECODER : AVOX_FF_VP9_DECODER;
#elif __APPLE__
    // Apple 走 VideoToolbox(IOSVDecoder::onVaild 探测 VTIsHardwareDecodeSupported)
    return bHard ? AVOX_IOS_VP9_DECODER : AVOX_FF_VP9_DECODER;
#elif defined(__ONLY_LINUX__)
    // Linux 走 Vulkan Video(FFVkDecoder::onVaild 探测 FFmpeg 侧 vulkan hwaccel
    // 编入, 驱动无 video 队列时运行期 openFailed 回退下一条候选)
    return bHard ? AVOX_FFVULKAN_VP9_DECODER : AVOX_FF_VP9_DECODER;
#else
    // 注册名来自 regFFCodec 的 codec->name
    return AVOX_FF_VP9_DECODER;
#endif
  } else if (codecId == VCodecId::av1) {
#if defined(__APPLE__)
    // AV1: Apple 走 VideoToolbox(M3/A17 Pro 起有硬解块, IOSVDecoder::onVaild
    // 探测 VTIsHardwareDecodeSupported, 不支持时运行期回退下一条候选)
    return bHard ? AVOX_IOS_AV1_DECODER : AVOX_FF_AV1_DECODER;
#elif defined(__ANDROID__)
    // AV1: Android 走 MediaCodec(8 Gen 2 起多数旗舰带 AV1 硬解块;
    // onVaild 按解码器名排除平台软实现 c2.android.av1)
    return bHard ? AVOX_ANDROID_AV1_DECODER : AVOX_FF_AV1_DECODER;
#elif defined(_WIN32)
    // AV1: Windows 走 D3D11VA(FFDx11Decoder::onVaild 探测 GPU 解码 profile)。
    // 注意 FFmpeg 的 av1 解码器是 hwaccel-only 包装, 无 dav1d 构建下"软解"名
    // 实际解不出帧 —— 该空壳的剔除在 VideoDecoderChain::build 里做
    return bHard ? AVOX_FFDX11_AV1_DECODER : AVOX_FF_AV1_DECODER;
#elif defined(__ONLY_LINUX__)
    // Linux 走 Vulkan Video(探测与降级同 vp9); 软解名在无 dav1d 构建下解不出帧
    return bHard ? AVOX_FFVULKAN_AV1_DECODER : AVOX_FF_AV1_DECODER;
#else
    // 其他平台暂无 AV1 硬解车道, 注册名来自 regFFCodec 的 codec->name
    //(FFmpeg 软解 AV1 需构建带 dav1d/libaom)
    return AVOX_FF_AV1_DECODER;
#endif
  } else if (codecId == VCodecId::prores) {
#if defined(__APPLE__)
    // ProRes: Apple 走 VideoToolbox(IOSVDecoder::onVaild 探测系统版本,
    // 建不出会话/不支持时运行期回退下一条候选)。硬解直通能省掉 4K 422 的
    // 整帧软解开销(实测软解 ~34fps 已够播, 硬解主要省 CPU)
    return bHard ? AVOX_IOS_PRORES_DECODER : AVOX_FF_PRORES_DECODER;
#else
    // ⚠️ Windows/Linux/Android **均无 ProRes 硬解**(不在 DXVA/VAAPI/MediaCodec
    // 名单) ⇒ 恒软解。此处显式给出软解名 —— 老写法不给会落到本函数末尾的
    // AVOX_FF_H264_DECODER, 再靠"找不到名字用 decodes[0]"侥幸选中, 那是隐式
    // 行为; 候选表改成注册表全量后该侥幸路径已不存在
    return AVOX_FF_PRORES_DECODER;
#endif
  } else if (codecId == VCodecId::vvc) {
    // VVC: 三平台均无硬解(DXVA/MediaCodec/VT 名单没有) ⇒ 恒软解
    return AVOX_FF_VVC_DECODER;
  } else if (codecId == VCodecId::mjpeg) {
    // MJPEG: 三平台均无硬解(MJPG 摄像头那套是 MF/AVFoundation 设备侧转码,
    // 不走本引擎解码器登记) ⇒ 恒软解, 与 prores/vvc 同理须显式给名
    return AVOX_FF_MJPEG_DECODER;
  }
  return AVOX_FF_H264_DECODER;
}

}
