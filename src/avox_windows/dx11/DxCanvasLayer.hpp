#pragma once

#include <d3d11.h>
#include <wrl/client.h>

#include <vector>

#include "avox/subtitle/CanvasState.hpp"
#include "avox/subtitle/SubtitleCanvas.hpp"

namespace avox {

template <typename T>
using MComPtr = Microsoft::WRL::ComPtr<T>;

// DX11 字幕画布层(字幕画布多后端渲染计划 §5.1): 数据面在 CanvasState(多后端
// 共享), 本层做 D3D11 纹理上传与参数常量。混合由 CS canvas 变体完成(第二 SRV
// t2 + cbuffer b1, 写 UAV 前逐像素叠加), 无内容不绑不分发(零字幕零影响)。
// HDR 直通(R10G10B10A2, PQ 码域)走 CPU 侧 PQ 预编码画布(拍板 §二.1, LUT 见
// CanvasBlendMath.hpp)——GPU 混合代码与 SDR 完全同一条; 域翻转重编码不上抛。
class DxCanvasLayer : public ICanvasLayer {
 public:
  // ICanvasLayer(数据面全委托 CanvasState)
  void updateCanvas(const AssCanvas& canvas) override;
  void clearCanvas() override;
  void setCanvasTransform(float scale, float offsetX, float offsetY,
                          float opacity) override;

  // 渲染线程: 建纹理/画布(尺寸驱动), 编码并上传内容, 刷参数常量。
  // pq=true 时画布预编码为 PQ 码(直通域), false 原样 SDR gamma。
  // 返回 false = 未就绪(调用方走非 canvas 变体, 字幕本帧缺帧)
  bool prepareAndBind(ID3D11Device* device, ID3D11DeviceContext* context,
                      int32_t frameW, int32_t frameH, bool pq);

  bool visible() const { return state.hasContent(); }
  ID3D11ShaderResourceView* srv() const { return canvasSrv.Get(); }
  ID3D11Buffer* paramBuffer() const { return paramBuf.Get(); }

 private:
  // 内容/域变化时重编码 + 整画布上传
  bool uploadCanvas(ID3D11DeviceContext* context, bool pq);

  CanvasState state;
  MComPtr<ID3D11Texture2D> canvasTex;
  MComPtr<ID3D11ShaderResourceView> canvasSrv;
  MComPtr<ID3D11Buffer> paramBuf;
  // PQ 预编码输出(与 CanvasState 的 raw 画布 1:1)
  std::vector<uint8_t> encoded;
  int32_t encodedW = 0;
  int32_t encodedH = 0;
  bool bEncodedPq = false;
  uint8_t pqLut[256];
  bool bLutBuilt = false;
};

// canvas 变体常量区(与 CS 里 cbuffer CanvasCbuf : register(b1) 布局严格同序)
struct DxCanvasCbuf {
  // rect = 门控矩形 center.xy + size.xy(帧归一化); xform = origin.xy,
  // invScale, opacity; misc = canvasW, canvasH(16B 对齐补位)
  float rect[4];
  float xform[4];
  float misc[4];
  float pad[4];
};

}
