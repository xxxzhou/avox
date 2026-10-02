#pragma once

#include <atomic>

#include "../dx12/Dx12Helper.hpp"
#include "Dx11Resource.hpp"
#include "DxCanvasLayer.hpp"
#include "avox/subtitle/CanvasRender.hpp"
#include "avox/video/ColorSpace.hpp"
#include "avox/video/VideoBuffer.hpp"
#include "avox/video/VideoRender.hpp"

namespace avox {

// 把NV12/P010纹理转换为RGBA8纹理(P010路径内置 HDR tone map, 与 glsl
// yuv2rgbaV5.comp 同源: PQ/HLG 解码 + ACES + BT.2020->BT.709)
class Dx11CSVideoRender : public VideoRender, public Dx11Context {
 public:
  Dx11CSVideoRender();
  virtual ~Dx11CSVideoRender() {};

 protected:
  // YUV 2 RGBA8 shader(基础变体)
  MComPtr<ID3D11ComputeShader> computeShader = nullptr;
  // DV 变体: 首个 DV 帧惰性编译(冷编译 ~1.5s, 非 DV 内容零成本)
  MComPtr<ID3D11ComputeShader> dvShader = nullptr;
  ID3D11ComputeShader* boundShader = nullptr;
  bool bDvProgramTried = false;
  // 计算着色器资源
  // std::unique_ptr<Dx11Texture> outTexture = nullptr;
  // 输出共享纹理
  std::unique_ptr<Dx11SharedTex> outSharedTex = nullptr;
  Dx11Texture* outTexture = nullptr;
  // 常量缓冲区
  std::unique_ptr<Dx11Constant> constBuf = nullptr;
  // 解码的纹理没有D3D11_BIND_SHADER_RESOURCE,不能直接生成SRV
  MComPtr<ID3D11Texture2D> inTexture = nullptr;
  MComPtr<ID3D11ShaderResourceView> yView = nullptr;
  MComPtr<ID3D11ShaderResourceView> uvView = nullptr;
  uint32_t imageWidth = 0;
  uint32_t imageHeight = 0;
  D3D11_TEXTURE2D_DESC yuvDesc = {};
  // 颜色/HDR 参数(setColorSpace/setHdrMeta/setHdrMode 注入, 随脏标记进常量):
  // P010 硬解的 tone map 在本 CS 内完成(与 glsl yuv2rgbaV5.comp 同源)
  ColorSpaceDesc cs{YuvStandard::bt601, YuvRange::full};
  HdrMeta hdrMeta = {};
  HdrMode hdrMode = HdrMode::follow;
  DoviMeta doviMeta = {};
  bool bParamsDirty = true;
  // 常量区与 glsl ColorYuvUBO 同布局(96B 头 + DV 区), cbuffer 声明对齐
  ColorYuvUBO constData{};
  // CPU NV12直取(bOutCpuYuv时): 复用staging纹理,映射指针零拷发布
  // Unmap顺延到下一帧回读,消费者须在当帧窗口内使用
  MComPtr<ID3D11Texture2D> stagingTexture = nullptr;
  ImageBuffer stagingBuffer;
  bool bStagingMapped = false;
  uint32_t publishedTick = 0;
  int32_t stagingWidth = 0;
  int32_t stagingHeight = 0;
  // CPU 帧腿(G9): cpuIn 时输入不是解码纹理, 自建 NV12/P010 上传纹理当 CS 源。
  // 值 = 该纹理的 DXGI 格式(NV12/P010), UNKNOWN 表示非 CPU 建图
  DXGI_FORMAT cpuInFormat = DXGI_FORMAT_UNKNOWN;
  // CPU 帧腿的 D3D11 设备(取自呈现窗口, 渲染线程持有; 非拥有, 不 Release)
  ID3D11Device* cpuDevice = nullptr;
  // 字幕画布(字幕画布多后端渲染计划 §5.1): 宿主持前端(wanted 标志), 渲染线程
  // 持层实例; canvas 变体惰性编译(withDv×withCanvas 四象限只补 canvas 两象限)
  std::unique_ptr<CanvasRender> canvasRender;
  std::unique_ptr<DxCanvasLayer> canvasLayer;
  std::atomic<bool> bCanvasWanted{false};
  MComPtr<ID3D11ComputeShader> canvasShader;
  MComPtr<ID3D11ComputeShader> canvasDvShader;
  MComPtr<ID3D11SamplerState> canvasSampler;
  bool bCanvasProgramTried = false;
  bool bCanvasDvProgramTried = false;

 protected:
  // 初始化图形管线
  virtual bool vaildAndInitGraph() override;
  virtual void releaseGraph() override;
  virtual void renderGpuFrame(const GpuFrame& frame) override;
  // CPU 帧腿(G9): yuv420P/yuv420P10 平面 → NV12/P010 上传纹理 → 既有 CS
  virtual void renderCpuFrame(const YUVFrame& frame) override;
  // 字幕画布挂/摘同步(渲染线程消费 bCanvasWanted, 内容信号走 CanvasRender)
  void syncCanvasLayer();
  // canvas 变体惰性编译(百毫秒级冷编译只付一次, 与 DV 同策略)
  bool ensureCanvasProgram(bool withDv);
  // 变体选择 + canvas 资源绑定(render 线程); allowDv=false 供 CPU 帧腿
  ID3D11ComputeShader* selectShader(bool allowDv);
  virtual bool fetchFrame(ImageBuffer* imageBuffer) override;
  // 颜色/HDR 参数(VideoRender 虚接口), 触发常量脏标记
  virtual void setColorSpace(const ColorSpaceDesc& c) override;
  virtual void setHdrMeta(const HdrMeta& meta) override;
  virtual void setDoviMeta(const DoviMeta& meta) override;
  virtual void setHdrMode(HdrMode mode) override;
  // bOutCpuYuv时把当前NV12帧staging回读,渲染线程内按需调用,一帧最多一次
  virtual bool getCpuFrameBuffer(IImageBuffer** buffer, YuvType& yuvType,
                                 int64_t* pts) override;

 public:
  // 字幕画布挂口(字幕画布多后端渲染计划 §5.1): lane=0 本腿输出是 VK 对接面
  // 禁挂(字幕由 VK canvas 层负责); lane=1 返回稳定前端, CS canvas 变体合成
  virtual ICanvasLayer* enableRenderCanvas() override;
  virtual void disableRenderCanvas() override;

 public:
  virtual IRenderContext* getGpuContext() override {
    return outSharedTex.get();
  }

 public:
  void createProgram();
  void renderToTexture(const GpuFrame& gpuFrame);
  // DV 变体按需编译; 返回本帧应绑定的着色器(DV 未就绪回退基础)
  bool ensureDvProgram();
  ID3D11ComputeShader* selectShader();
  // staging拷贝+Map+零拷发布到stagingBuffer,失败返回false
  bool mapStagingFrame();
  // CPU 帧腿建图: 自建 NV12/P010 输入纹理 + 取窗口设备 + createProgram
  bool initGraphCpu(const YUVFrame& frame);
  // 把 CPU 平面收进 inTexture(NV12 直拷 / P010 <<6), 失败返回 false
  bool uploadCpuPlanes(const YUVFrame& frame);
};
}