#include "QEnhanceFilter.hpp"

#include <cstdlib>
#include <vector>

#include "../module/LogHelper.hpp"
#include "../video/CpuQEnhancer.hpp"

namespace avox {

QEnhanceFilter::QEnhanceFilter(const QualityEnhanceParamet& paramet) {
  this->paramet = paramet;
}

QEnhanceFilter::~QEnhanceFilter() {
  // enableImage契约: buf生命周期需维持到disableImage之后; 关掉原close()
  // 直接reset缓冲留下的悬存窗口(下次open重建前surfaceRender图分支仍指旧址)
  if (render) {
    render->disableImage();
  }
}

// 增强要解码帧且要有封装目标: 空输出(离屏直出)与视频直拷下无意义
bool QEnhanceFilter::accepts(bool bNoOutput, bool bVideoCopy) const {
  return !bNoOutput && !bVideoCopy;
}

bool QEnhanceFilter::prepare(const VideoDesc& srcDesc, bool bHardEncode,
                             WindowRender* render, VideoDesc& outDesc) {
  this->render = render;
  enhancer = std::make_unique<CpuQEnhancer>();
  // 增强输出 yuv 类型必须与编码模式匹配(硬编 nv12/软编 yuv420P), 否则
  // FFVEncoder 按(codecCtx->pix_fmt)nv12 读 yuv420p 平面 → 越界崩溃
  YuvType enhanceOut = bHardEncode ? YuvType::nv12 : YuvType::yuv420P;
  if (!enhancer->init(paramet, srcDesc.width, srcDesc.height, enhanceOut)) {
    // 模型/后端缺失: 返回false让录制器清滤镜回退普通转码
    enhancer.reset();
    return false;
  }
  // 离线画质增强: 图只做 yuv→rgba(enableImage 独立输出分支), rgba 帧入队,
  // 编码线程侧推理; 输出尺寸由增强器决定, yuv 分支不开。
  rgbaBuffer = std::make_shared<ImageBuffer>();
  ImageFormat fmt = {};
  // [dbg] 稳定性实验: rgba 缓冲回退源分辨率(GPU resize 无缩放), 推理
  // 分辨率的降采样由 CpuQEnhancer 的 box filter 在 CPU 侧完成
  fmt.width = srcDesc.width;
  fmt.height = srcDesc.height;
  fmt.imageType = ImageType::rgba8;
  rgbaBuffer->setImageFormat(fmt);
  static const bool noImg = std::getenv("ENH_NOIMG") != nullptr;
  render->setOffSurface(YuvType::other);
  if (!noImg) {
    render->enableImage(rgbaBuffer.get());
  }
  // 关键: enableImage 的图重建延迟到下一次 render, 首个真帧的 render 期间
  // 重建会销毁旧 cpuBuffer, rgbaBuffer(引用旧映射内存)在重建后到下一帧输出
  // 前是悬存的 → 拷贝帧时读已释放内存。此处先渲染一张空帧, 让「带 image
  // 分支的重建」同步完成且 rgbaBuffer 引用落位, 之后的真帧拷贝安全
  static const bool noDummy = std::getenv("ENH_NODUMMY") != nullptr;
  if (!noDummy) {
    int32_t sw = srcDesc.width, sh = srcDesc.height;
    size_t ySize = (size_t)sw * sh;
    size_t uvSize = (size_t)(sw / 2) * (sh / 2);
    std::vector<uint8_t> dummy(ySize + uvSize * 2, 0);
    YUVFrame dummyFrame = {};
    dummyFrame.format.width = sw;
    dummyFrame.format.height = sh;
    dummyFrame.format.type = YuvType::nv12;
    dummyFrame.data[0] = dummy.data();
    dummyFrame.stride[0] = sw;
    dummyFrame.data[1] = dummy.data() + ySize;
    dummyFrame.stride[1] = sw;
    render->render(dummyFrame);
    LOGFLF(LogLevel::info, "quality enhance dummy frame rendered, ",
           "graph rebuilt with image branch");
  }
  outDesc = srcDesc;
  outDesc.width = enhancer->outWidth();
  outDesc.height = enhancer->outHeight();
  outDesc.type = enhanceOut;
  LOGFLF(LogLevel::info, "quality enhance path, out:", outDesc.width, "x",
         outDesc.height);
  return true;
}

bool QEnhanceFilter::capture(int64_t pts, int64_t dts, VideoFramePtr& out) {
  // [dbg] 竞态排查: ENH_NOQUEUE=1 跳过入队, 仅验 render/图分支稳定性
  static const bool noQueue = std::getenv("ENH_NOQUEUE") != nullptr;
  if (noQueue || !rgbaBuffer) {
    return false;
  }
  RgbaFrameRef ref = {rgbaBuffer.get(), pts, dts};
  copyBufRgba(out, ref);
  return true;
}

bool QEnhanceFilter::process(const VideoFramePtr& in, YUVFrame& out) {
  if (!enhancer || !in || !in->buffer) {
    return false;
  }
  // 队列帧恒为capture深拷的rgba8(SwVideoBuffer兼作ImageBuffer), 逐帧推理
  // (推理慢则队列满反压解码, 整线按推理速度节拍)
  auto* rgba = static_cast<SwVideoBuffer*>(in->buffer.get());
  return enhancer->process(rgba, in->pts, in->dts, out);
}

// 工厂: 录制器经此注入, 不感知具体滤镜类型
std::shared_ptr<IRecordVideoFilter> createQualityEnhanceFilter(
    const QualityEnhanceParamet& paramet) {
  return std::make_shared<QEnhanceFilter>(paramet);
}

}
