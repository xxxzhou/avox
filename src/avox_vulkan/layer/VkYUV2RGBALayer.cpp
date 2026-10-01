#include "VkYUV2RGBALayer.hpp"

#include <cstdio>
#include <cstring>

#include "VkPipeGraph.hpp"
namespace avox {

VkYUV2RGBALayer::VkYUV2RGBALayer(/* args */) { setUBOSize(sizeof(ColorYuvUBO)); }

VkYUV2RGBALayer::~VkYUV2RGBALayer() {}

void VkYUV2RGBALayer::onUpdateParamet() {
  if (paramet == oldParamet) {
    return;
  }
  resetGraph();
}

void VkYUV2RGBALayer::refreshColorMat() {
  uboData.colorMat = buildYuvToRgb(cs);
  // transfer 随 cs 运行时更新: setColorSpace 只走本函数, 只写 onInitLayer 会丢晚到的标志
  uboData.transfer = (int32_t)cs.transfer;
  uboData.hdrMode = (int32_t)hdrMode;
  LOGFLF(LogLevel::info, "[yuv2rgba] ubo transfer:", uboData.transfer,
         " peak:", uboData.maxLuminance, " sdrWhite:", uboData.sdrWhiteNits,
         " hdrMode:", uboData.hdrMode);
  updateUBO(&uboData);
}

void VkYUV2RGBALayer::setColorSpace(const ColorSpaceDesc& c) {
  if (c.standard == cs.standard && c.range == cs.range &&
      c.transfer == cs.transfer) {
    return;  // 幂等: 渲染线程每轮收敛直推, 无变化早退
  }
  cs = c;
  refreshColorMat();
  // 运行时重传 UBO, 不触发 graph 重建
  bParametChange = true;
}

void VkYUV2RGBALayer::setHdrMeta(const HdrMeta& meta) {
  if (!meta.valid) {
    return;
  }
  if (lastMeta.valid == meta.valid && lastMeta.maxCLL == meta.maxCLL &&
      lastMeta.maxLuminance == meta.maxLuminance &&
      lastMeta.l1MaxNits == meta.l1MaxNits) {
    return;  // 幂等: 渲染线程每轮收敛直推, 无变化早退
  }
  lastMeta = meta;
  uboData.maxLuminance = (float)hdrPeakNits(meta);
  // 探针走 stderr: playtest 环境 logTask 启动后不再排水, 引擎 info 日志不可见
  fprintf(stderr, "[yuv2rgba] setHdrMeta peak=%.1f l1max=%.1f cll=%u\n",
          (double)uboData.maxLuminance, (double)meta.l1MaxNits, meta.maxCLL);
  // updateUBO 写 CPU 暂存, 真正上传在 onPreFrame(需 bParametChange); 漏暂存则
  // 晚到的元数据静默失效——onPreFrame 上传的是暂存副本, 不是本结构
  updateUBO(&uboData);
  bParametChange = true;
}

void VkYUV2RGBALayer::setHdrMode(HdrMode mode) {
  if (mode == hdrMode) {
    return;
  }
  LOGFLF(LogLevel::info, "[yuv2rgba] hdrMode:", (int32_t)mode,
         " (0=follow 1=forceSDR 2=forceHDR)");
  // 不在此 resetGraph: 直通过界的拓扑重建由 VkVideoRender::setHdrMode 置
  // bResetFlag 驱动(渲染线程消费), 本函数只落模式+UBO(10/1 UAF 案口径)
  hdrMode = mode;
  refreshColorMat();
  bParametChange = true;
}

void VkYUV2RGBALayer::setDoviMeta(const DoviMeta& meta) {
  if (0 == std::memcmp(&doviMeta, &meta, sizeof(DoviMeta))) {
    return;  // 幂等: 渲染线程每轮收敛直推, 无变化早退
  }
  // 探针走 stderr: playtest 环境 logTask 启动后不再排水, 引擎 info 日志不可见
  fprintf(stderr, "[yuv2rgba] setDoviMeta valid=%d pivots=%d/%d/%d\n",
          (int)meta.valid, (int)meta.comp[0].numPivots,
          (int)meta.comp[1].numPivots, (int)meta.comp[2].numPivots);
  doviMeta = meta;
  packDoviUbo(uboData, doviMeta);
  updateUBO(&uboData);
  bParametChange = true;
}

void VkYUV2RGBALayer::onInitLayer() {
  YuvType yuvType = paramet;
  // nv12/yuv420P/yuy2P
  std::string path = "glsl/yuv2rgbaV1.comp.spv";
  if (yuvType == YuvType::yuv2I || yuvType == YuvType::yvyuI ||
      yuvType == YuvType::uyvyI) {
    path = "glsl/yuv2rgbaV2.comp.spv";
  }
  if (yuvType == YuvType::uyvy422_10B) {
    path = "glsl/yuv2rgbaV3.comp.spv";
  }
  if (yuvType == YuvType::yuv420P10 || yuvType == YuvType::p010) {
    if (hdrMode == HdrMode::forceHDR) {
      // 直通变体(vk-hdr-lane.md V1): 输出扩展线性域(1.0=SDR 白)。
      // 输出格式不在此设 —— 下面的共用归一化段会盖回 rgba8(G4), 改在段末收口
      path = "glsl/yuv2rgbaHDR.comp.spv";
    } else {
      // V5 通吃 10bit(含 p010 上传归一化): transfer 为运行时 UBO 分支, SDR 直通零改动
      path = "glsl/yuv2rgbaV5.comp.spv";
    }
  }
  fprintf(stderr, "[yuv2rgba] onInitLayer variant=%s yuvType=%d\n",
          path.c_str(), (int)yuvType);
  shader->loadShaderModule(path);
  assert(shader->shaderStage.module != VK_NULL_HANDLE);
  // 带P/SP的格式由r8转rgba8
  inFormats[0].imageType = ImageType::r8;
  outFormats[0].imageType = ImageType::rgba8;
  if (paramet == YuvType::nv12 || paramet == YuvType::yuv420P) {
    outFormats[0].height = inFormats[0].height * 2 / 3;
    // 一个线程处理四个点
    sizeX = divUp(outFormats[0].width, 2 * groupX);
    sizeY = divUp(outFormats[0].height, 2 * groupY);
  } else if (paramet == YuvType::yuv420P10 || paramet == YuvType::p010) {
    // 输入侧: 输入层把 10bit 平面字流(u16/字)转成 RGBA8 字节视图(每 texel=2
    // 个字, 高 0.75×帧高), shader V5 拆字节还原; 输出高 = 输入高*4/3 整型还原
    // (字行数 = 3h/2, texel 行数 = ceil(3h/4), 对 h%4∈{0,2} 均精确还原)
    inFormats[0].imageType = ImageType::rgba8;
    outFormats[0].height = inFormats[0].height * 4 / 3;
    // 一个线程处理四个点
    sizeX = divUp(outFormats[0].width, 2 * groupX);
    sizeY = divUp(outFormats[0].height, 2 * groupY);
  } else if (paramet == YuvType::yuv422P ||
             paramet == YuvType::yuyv422A) {
    outFormats[0].height = inFormats[0].height / 2;
    // 一个线程处理二个点
    sizeX = divUp(outFormats[0].width, 2 * groupX);
    if (paramet == YuvType::yuv422P) {
      sizeY = divUp(outFormats[0].height, groupY);
    } else {
      sizeY = divUp(outFormats[0].height, 2 * groupY);
    }
  } else if (paramet == YuvType::yuv2I || paramet == YuvType::yvyuI ||
             paramet == YuvType::uyvyI) {
    inFormats[0].imageType = ImageType::rgba8;
    // 一个线程处理二个点,yuyv四点组合成一个元素,和rgba类似
    outFormats[0].width = inFormats[0].width * 2;
    sizeX = divUp(inFormats[0].width, groupX);
    sizeY = divUp(inFormats[0].height, groupY);
  } else if (paramet == YuvType::uyvy422_10B) {
    // 一个线程处理二个点,对应yuv422_10B中的五个字节
    outFormats[0].width = inFormats[0].width * 2 / 5;
    sizeX = divUp(outFormats[0].width, 2 * groupX);
    sizeY = divUp(outFormats[0].height, groupY);
  }
  // 直通变体输出格式收口(G4): 上面共用归一化段会按输入类型把 outFormats[0]
  // 重置成 rgba8, 故 16F 覆盖必须放在它之后 —— 否则 yuv2rgbaHDR 写出的 >1 线性
  // 值进 8bit 纹理被钳 1.0, 高光全丢。仅 HDR 变体(10bit + forceHDR)需要
  if ((paramet == YuvType::yuv420P10 || paramet == YuvType::p010) &&
      hdrMode == HdrMode::forceHDR) {
    outFormats[0].imageType = ImageType::rgba16f;
  }
  // 填 UBO 头字段 + 矩阵(随 cs)
  uboData.width = outFormats[0].width;
  uboData.height = outFormats[0].height;
  uboData.yuvType = (int32_t)yuvType;
  refreshColorMat();
}

}
