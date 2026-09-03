#pragma once

#include <future>
#include <mutex>
#include <string>

#include "../AvoxMath.h"
#include "../module/JsonOption.hpp"
#include "../player/Player.hpp"
#include "../video/Window.hpp"
#include "VideoFrame.hpp"

namespace avox {

struct VRenderDesc {
  std::string name = "VideoRender";
};

// 视频渲染主要负责平台窗口渲染
// 整合VideoGraph,自己不Tick,由窗口Tick
// 1. 各平台硬解的原生GPU数据，转为RGBA8格式的GPU数据
// 2. 根据窗口呈现，确定是渲染到窗口还是纹理上。
// 3. 包含原生DX11/OpenGL/Metal与Vulkan交互
// VideoRender由windows窗口驱动,每次Tick检查是否需要重置,脱离状态
// 考虑会渲染解码帧的情况,其解码线程与渲染线程可能不同
// 也有可能渲染线程拿取设备直出的GPU数据
// 故要考虑输入帧与渲染不同GPU上下文的情况
class AVOX_EXPORT VideoRender : public OptionLink {
 public:
  VideoRender();
  virtual ~VideoRender();

 protected:
  // 对应各平台渲染窗口，如果为空，则可能是渲染到FBO
  AvoxSurfaceType surface = nullptr;
  // 窗口格式，窗口有大小，则输出窗口的大小
  ImageFormat windowFormat = {};
  // 窗口为空，渲染离屏的大小
  ImageFormat imageFormat = {};
  // 重置标志，窗口大小改变，GPU上下文改变可能都需要重置
  bool bResetFlag = false;
  // 渲染类型，opengles/vulkan/metal/dx11
  RenderType renderType = RenderType::other;

  // 是否支持CPU输出YUV数据
  bool bOutCpuYuv = false;
  // 软编一般要求输出为YUV420P格式,硬编是NV12格式
  YuvType outYuvType = YuvType::yuv420P;
  // 是否每帧输出处理后的图像到外部 IImageBuffer (enableImage)
  bool bEnableImage = false;
  ImageFormat imageOutFormat = {};
  IImageBuffer* imageOutBuffer = nullptr;  // 不持有, 调用方保证生命周期

  // 这三个值在每帧渲染时，保持帧，只在渲染线程中使用
  bool cpuIn = false;
  // RGBA直接输入(跳过yuv2RGBA层),IImageBuffer输入时为true
  bool bRgbaInput = false;
  YUVFrame yuvFrame = {};
  GpuFrame gpuFrame = {};

  // 调用截图，需要同步调用
  std::mutex mtxShot;
  std::atomic<bool> bShotFlag = false;
  ImageBuffer* imageBuffer = nullptr;
  // 一次截图使用一次，每次使用需要重新创建
  std::shared_ptr<std::promise<bool>> bShotComplete;

  // 长宽比保持,如果是0,全屏填满
  bool bFullScreen = false;
  float aspect = 0.0f;

 public:
  RenderType getRenderType() { return renderType; }
  ImageFormat getImageFormat() { return imageFormat; }
  // 修改是否支持CPU输出,用于把处理后的图像软编
  void enableYuvOut(YuvType yuvType = YuvType::yuv420P);
  void disableYuvOut();
  // 每帧把处理后的帧按 buf 的 ImageFormat(尺寸+格式) 经 GPU 缩放后零拷贝写入 buf
  // 当前仅 VkVideoRender 实现; imageType 需为 rgba8
  void enableImage(IImageBuffer* buf);
  void disableImage();
  bool bImageOut() { return bEnableImage; }
  // 是否支持CPU输出
  bool bCpuOut() { return bOutCpuYuv; }
  // 改变窗口，会重置bResetFlag,这样会在运行时重置
  void setSurface(AvoxSurfaceType surface);
  // 得到窗口
  AvoxSurfaceType getSurface() { return surface; }
  // 是否全屏,如果不是,根据要渲染的长宽自适应
  void setFullScreen(bool bFull);
  // 设定长宽比
  void setAspect(float aspect);
  // 这函数在窗口线程运行，需要需要调用releaseGraph
  // 检查GPU资源是否已准备就绪，没有就调用initGraph
  void renderFrame(const avox::VideoFrame& videoBuffer);
  // 把DX11/OpenGL/Metal的NV12纹理转为RGBA8纹理
  void renderFrame(const GpuFrame& frame);
  // 暂时所有平台只有vulkan会处理CPU帧
  void renderFrame(const YUVFrame& frame);
  // 静态图像渲染,直接输入IImageBuffer(rgba8/bgra8等格式)
  void renderFrame(IImageBuffer* buffer);
  // 这个是Vk调用，把DX11/OpenGL/Metal的RGBA纹理映射为Vk的GPU纹理
  void renderFrame(const avox::VideoFrame& videoBuffer, IRenderContext* context);
  // 抓帧发起一般不在渲染线程，所以这需要别的线程调用设置flag
  // 在渲染线程检测到flag后，调用fetchFrame,并发送通知
  // 发起线程在得到通知后，返回数据，这样调用方可在任意数据同步获取数据
  // 设置最多2秒超时，最长可能堵塞调用方2秒
  bool screenShot(ImageBuffer* imageBuffer);
  void checkShot();
  // 渲染线程关闭，重置资源，画面成空白
  void closeResource();

 private:
  // 上面的renderFrame最终会调用这个
  void renderFrame();

 protected:
  virtual void onSetSurface() {};
  virtual void onSurfaceSizeChange(int32_t wdWidth, int32_t wdHeight) {};
  virtual bool vaildAndInitGraph() { return false; };
  virtual void releaseGraph() {};
  virtual void renderGpuFrame(const GpuFrame& frame) {};
  virtual void renderCpuFrame(const YUVFrame& frame) {};
  // 静态图像CPU输入(rgba8/bgra8等),暂时只有VkVideoRender实现
  virtual void renderCpuFrame(IImageBuffer* buffer) {};
  // 抓取当前帧数据保存到ImageBuffer,GPU-CPU,带宽占用大，不适合Tick使用
  // renderFrame中调用，保证在渲染线程中
  virtual bool fetchFrame(ImageBuffer* imageBuffer) { return false; };

  // 主要拿到处理后的图像
 public:
  // 当bOutCpuYuv为true,返回处理后的YUV资源
  virtual bool getCpuFrame(YUVFrame& frame);
  virtual bool getGpuFrame(GpuFrame& frame);
  // 把Vk处理后的GPU资源映射到Dx11/OpenGL/Metal的GPU资源
  virtual bool outputGpuFrame(IRenderContext* ctx) { return false; }

  // 跨SDK的GPU资源交互
 public:
  // DX11/OpenGL/Metal的RGBA纹理GPU数据上下文
  virtual IRenderContext* getGpuContext() = 0;
  // 把DX11/OpenGL/Metal的RGBA纹理GPU映射到Vulkan的GPU数据
  virtual void renderGpuFrame(IRenderContext* context) {};
  //  Vk/dx11输出到窗口,Vk计算与呈现分离
  virtual void renderWindow(Window* window) {};
  // 各平台GPU输出buffer
  virtual void* getOutGpuBuffer() { return nullptr; }

  // IOption
 public:
  virtual void onOptionChange(const char* key, ArgType option) override;
};

/**
 * @param swidth  窗口宽度
 * @param sheight 窗口高度
 * @param aspect  视频长宽比 (width / height)
 */
vec4i getViewRect(int swidth, int sheight, float aspect);
/**
 * @param texWidth  纹理宽度
 * @param texHeight 纹理高度
 * @param aspect    窗口长宽比 (width / height)
 */
vec4i getTextureRect(int texWidth, int texHeight, float aspect);

}