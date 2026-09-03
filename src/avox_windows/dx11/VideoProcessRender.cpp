#include "VideoProcessRender.hpp"

#include "Dx11Window.hpp"
#include "avox/module/AvoxManager.hpp"

#if AVOX_ENABLE_FFMPEG
#include "avox_ffmpeg/FFCommon.hpp"
#endif

namespace avox {

VideoProcessRender::VideoProcessRender() { renderType = RenderType::D3D11; }

VideoProcessRender::~VideoProcessRender() { releaseGraph(); }

bool VideoProcessRender::vaildAndInitGraph() {
  if (!gpuFrame.buffer || cpuIn) {
    return false;
  }
  Dx11Context* context = static_cast<Dx11Context*>(gpuFrame.context);
  // 如果上下文或是大小变化，重新创建
  if (device != context->getDevice() || bResetFlag) {
    releaseGraph();
  }
  if (videoProcessor && !bResetFlag) {
    return true;
  }
  ID3D11Texture2D* yuvTexture = (ID3D11Texture2D*)gpuFrame.buffer;
  D3D11_TEXTURE2D_DESC desc = {};
  yuvTexture->GetDesc(&desc);
  // 纹理可能无效，比如源纹理重建了，在渲染线程中指针还在
  if (desc.Width == 0 || desc.Height == 0) {
    return false;
  }
  // 使用解码的D3D11设备
  setDevice(context->getDevice());
  imageWidth = gpuFrame.format.width;
  imageHeight = gpuFrame.format.height;
  inDesc = desc;
  createVideoProcessor();
  return videoProcessor != nullptr;
}

void VideoProcessRender::releaseGraph() {
  if (videoProcessor) {
    videoProcessor.Reset();
  }
  if (videoProcessorEnumerator) {
    videoProcessorEnumerator.Reset();
  }
  if (outputView) {
    outputView.Reset();
  }
  if (inputView) {
    inputView.Reset();
  }
  if (videoContext) {
    videoContext.Reset();
  }
  if (videoDevice) {
    videoDevice.Reset();
  }
  if (inTexture) {
    inTexture.Reset();
  }
  if (yView) {
    yView.Reset();
  }
  if (uvView) {
    uvView.Reset();
  }
  if (outSharedTex) {
    outSharedTex.reset();
  }
}

void VideoProcessRender::renderGpuFrame(const GpuFrame& frame) {
  renderToTexture(frame);
}

void VideoProcessRender::createVideoProcessor() {
  if (!device || !d3dcontext) {
    return;
  }
  HRESULT hr = S_OK;
  // 获取 Video Device
  hr = device->QueryInterface(__uuidof(ID3D11VideoDevice), &videoDevice);
  if (FAILED(hr)) {
    LOGFLF(LogLevel::warn, "QueryInterface ID3D11VideoDevice failed:", hr);
    return;
  }
  // 获取 Video Context
  hr = d3dcontext->QueryInterface(__uuidof(ID3D11VideoContext), &videoContext);
  if (FAILED(hr)) {
    LOGFLF(LogLevel::warn, "QueryInterface ID3D11VideoContext failed:", hr);
    return;
  }
  // 创建 Video Processor Enumerator
  D3D11_VIDEO_PROCESSOR_CONTENT_DESC contentDesc = {};
  contentDesc.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
  contentDesc.InputFrameRate.Numerator = 60;
  contentDesc.InputFrameRate.Denominator = 1;
  contentDesc.InputWidth = imageWidth;
  contentDesc.InputHeight = imageHeight;
  contentDesc.OutputFrameRate.Numerator = 60;
  contentDesc.OutputFrameRate.Denominator = 1;
  contentDesc.OutputWidth = imageWidth;
  contentDesc.OutputHeight = imageHeight;
  contentDesc.Usage = D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;

  hr = videoDevice->CreateVideoProcessorEnumerator(&contentDesc,
                                                   &videoProcessorEnumerator);
  if (FAILED(hr)) {
    LOGFLF(LogLevel::warn, "CreateVideoProcessorEnumerator failed:", hr);
    return;
  }
  // 创建 Video Processor
  hr = videoDevice->CreateVideoProcessor(videoProcessorEnumerator.Get(), 0,
                                         &videoProcessor);
  if (FAILED(hr)) {
    LOGFLF(LogLevel::warn, "CreateVideoProcessor failed:", hr);
    return;
  }
  // 初始化颜色空间
  ZeroMemory(&colorSpace, sizeof(colorSpace));
  colorSpace.Usage = 0;         // playback
  colorSpace.RGB_Range = 0;     // 0-255
  colorSpace.YCbCr_Matrix = 1;  // BT.709 (HD)
  colorSpace.YCbCr_xvYCC = 0;
  colorSpace.Nominal_Range = 1;  // 16-235 for Y, 16-240 for UV
  // 创建线程间共享输出纹理
  outSharedTex = std::make_unique<Dx11SharedTex>();
  Dx11Texture* outTexture = outSharedTex->getDx11Texture();
  outTexture->setTextureSize(imageWidth, imageHeight,
                             DXGI_FORMAT_R8G8B8A8_UNORM);
  outTexture->setVideoOut(true);
  outSharedTex->initTexture(device);

  // 创建输出 View
  D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC outputViewDesc = {};
  outputViewDesc.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
  outputViewDesc.Texture2D.MipSlice = 0;
  hr = videoDevice->CreateVideoProcessorOutputView(
      outTexture->texture.Get(), videoProcessorEnumerator.Get(),
      &outputViewDesc, &outputView);
  if (FAILED(hr)) {
    LOGFLF(LogLevel::warn, "CreateVideoProcessorOutputView failed:", hr);
    return;
  }
  // 创建输入复制纹理 - 需要添加 D3D11_BIND_RENDER_TARGET 标志用于
  // VideoProcessor
  D3D11_TEXTURE2D_DESC inCopyDesc = inDesc;
  inCopyDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
  inCopyDesc.MipLevels = 1;
  inCopyDesc.ArraySize = 1;
  inCopyDesc.SampleDesc.Count = 1;
  inCopyDesc.Usage = D3D11_USAGE_DEFAULT;
  hr = device->CreateTexture2D(&inCopyDesc, nullptr, &inTexture);
  if (FAILED(hr)) {
    LOGFLF(LogLevel::warn, "CreateTexture2D failed:", hr);
    return;
  }
  // 创建输入纹理的 SRV
  D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
  srvDesc.Format = DXGI_FORMAT_R8_UNORM;
  srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
  srvDesc.Texture2D.MipLevels = inCopyDesc.MipLevels;
  hr = device->CreateShaderResourceView(inTexture.Get(), &srvDesc, &yView);
  if (FAILED(hr)) {
    LOGFLF(LogLevel::warn, "CreateShaderResourceView (Y) failed:", hr);
    return;
  }
  srvDesc.Format = DXGI_FORMAT_R8G8_UNORM;
  hr = device->CreateShaderResourceView(inTexture.Get(), &srvDesc, &uvView);
  if (FAILED(hr)) {
    LOGFLF(LogLevel::warn, "CreateShaderResourceView (UV) failed:", hr);
    return;
  }
  // 创建输入 View
  D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC inputViewDesc = {};
  inputViewDesc.FourCC = 0;
  inputViewDesc.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
  inputViewDesc.Texture2D.MipSlice = 0;
  inputViewDesc.Texture2D.ArraySlice = 0;
  hr = videoDevice->CreateVideoProcessorInputView(
      inTexture.Get(), videoProcessorEnumerator.Get(), &inputViewDesc,
      &inputView);
  if (FAILED(hr)) {
    LOGFLF(LogLevel::warn, "CreateVideoProcessorInputView failed:", hr);
    return;
  }
  LOGFLF(LogLevel::info, "VideoProcessor created:", imageWidth, "x",
         imageHeight);
}

void VideoProcessRender::renderToTexture(const GpuFrame& gpuFrame) {
  Dx11Context* context = dynamic_cast<Dx11Context*>(gpuFrame.context);
  ID3D11Texture2D* yuvTexture = (ID3D11Texture2D*)gpuFrame.buffer;
  D3D11_TEXTURE2D_DESC desc = {};
  yuvTexture->GetDesc(&desc);
  if (!videoProcessor || !inTexture || !inputView || !outputView) {
    return;
  }
  // 复制输入纹理
  if (desc.ArraySize > 1) {
    // 复制指定索引的切片到临时纹理
    d3dcontext->CopySubresourceRegion(
        inTexture.Get(), D3D11CalcSubresource(0, 0, desc.MipLevels), 0, 0, 0,
        yuvTexture,
        D3D11CalcSubresource(0, gpuFrame.queueIndex, desc.MipLevels), nullptr);
  } else {
    // 将 yuvTexture 的数据复制给 inTexture
    d3dcontext->CopyResource(inTexture.Get(), yuvTexture);
  }
  // 使用 Video Processor 进行转换
  D3D11_VIDEO_PROCESSOR_STREAM stream = {};
  stream.Enable = TRUE;
  stream.OutputIndex = 0;
  stream.InputFrameOrField = 0;
  stream.pInputSurface = inputView.Get();

  // 设置源矩形和目标矩形 - 修复底部绿边问题
  RECT srcRect = {0, 0, (LONG)gpuFrame.format.width,
                  (LONG)gpuFrame.format.height};
  RECT dstRect = {0, 0, (LONG)gpuFrame.format.width,
                  (LONG)gpuFrame.format.height};

  videoContext->VideoProcessorSetStreamSourceRect(videoProcessor.Get(), 0, TRUE,
                                                  &srcRect);
  videoContext->VideoProcessorSetStreamDestRect(videoProcessor.Get(), 0, TRUE,
                                                &dstRect);
  videoContext->VideoProcessorSetStreamColorSpace(videoProcessor.Get(), 0,
                                                  &colorSpace);
  // if (!outSharedTex->canWrite()) {
  //   return;
  // }
  HRESULT hr = videoContext->VideoProcessorBlt(videoProcessor.Get(),
                                               outputView.Get(), 0, 1, &stream);
  outSharedTex->signalFence();
  if (FAILED(hr)) {
    LOGFLF(LogLevel::warn, "VideoProcessorBlt failed:", hr);
    return;
  }
  // outSharedTex->logTex();
}

bool VideoProcessRender::fetchFrame(ImageBuffer* imageBuffer) {
  if (!outSharedTex) {
    return false;
  }
  return fetchTexture(outSharedTex.get(), imageBuffer);
}

}
