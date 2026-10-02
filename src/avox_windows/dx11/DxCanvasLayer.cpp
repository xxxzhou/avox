#include "DxCanvasLayer.hpp"

#include <cstring>

#include "avox/module/LogHelper.hpp"
#include "avox/subtitle/CanvasBlendMath.hpp"

namespace avox {

void DxCanvasLayer::updateCanvas(const AssCanvas& canvas) {
  static bool bDiagLogged = false;
  if (!bDiagLogged) {
    bDiagLogged = true;
    LOGFLF(LogLevel::info, "dxcanvas first content: ", canvas.width, "x",
           canvas.height, " at ", canvas.x, ",", canvas.y);
  }
  state.updateCanvas(canvas);
}

void DxCanvasLayer::clearCanvas() { state.clearCanvas(); }

void DxCanvasLayer::setCanvasTransform(float scale, float offsetX,
                                       float offsetY, float opacity) {
  state.setCanvasTransform(scale, offsetX, offsetY, opacity);
}

bool DxCanvasLayer::prepareAndBind(ID3D11Device* device,
                                   ID3D11DeviceContext* context,
                                   int32_t frameW, int32_t frameH, bool pq) {
  static bool bDiagLogged = false;
  if (!bDiagLogged) {
    bDiagLogged = true;
    LOGFLF(LogLevel::info, "dxcanvas prepare first: frame=", frameW, "x",
           frameH, " hasContent=", state.hasContent() ? 1 : 0,
           " pending=", state.hasPending() ? 1 : 0);
  }
  if (!device || !context || frameW <= 0 || frameH <= 0) {
    return false;
  }
  // 画布 = 合成画布尺寸(subtitleCanvasSize, 与 VK 同机制)
  int32_t cw = 0;
  int32_t ch = 0;
  subtitleCanvasSize(frameW, frameH, &cw, &ch);
  if (cw <= 0 || ch <= 0) {
    return false;
  }
  if (!canvasTex || encodedW != cw || encodedH != ch) {
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = (UINT)cw;
    desc.Height = (UINT)ch;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(device->CreateTexture2D(&desc, nullptr, &canvasTex))) {
      canvasTex.Reset();
      return false;
    }
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;
    if (FAILED(device->CreateShaderResourceView(canvasTex.Get(), &srvDesc,
                                                &canvasSrv))) {
      canvasSrv.Reset();
      canvasTex.Reset();
      return false;
    }
    if (!state.reset(cw, ch)) {
      return false;
    }
    encodedW = cw;
    encodedH = ch;
    // 建图前来件暂存回灌(避免丢第一屏字幕, 与 VK applyPending 同流程)
    if (state.hasPending()) {
      std::vector<uint8_t> buf;
      int32_t w = 0, h = 0, stride = 0, x = 0, y = 0;
      state.takePending(buf, w, h, stride, x, y);
      state.updateCanvas(AssCanvas{buf.data(), w, h, stride, x, y});
    }
  }
  if (!state.hasContent()) {
    return false;  // 空窗: 调用方走非 canvas 变体(零字幕零影响)
  }
  if (!uploadCanvas(context, pq)) {
    return false;
  }
  if (!paramBuf) {
    D3D11_BUFFER_DESC bdesc = {};
    bdesc.ByteWidth = sizeof(DxCanvasCbuf);
    bdesc.Usage = D3D11_USAGE_DEFAULT;
    bdesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    if (FAILED(device->CreateBuffer(&bdesc, nullptr, &paramBuf))) {
      paramBuf.Reset();
      return false;
    }
  }
  // 门控矩形与采样反算每帧重算(静止帧改变换也即时生效); 混合码与域无关
  const CanvasBlendParamet p = state.computeParamet();
  DxCanvasCbuf cb = {};
  cb.rect[0] = p.centerX;
  cb.rect[1] = p.centerY;
  cb.rect[2] = p.width;
  cb.rect[3] = p.height;
  cb.xform[0] = p.originX;
  cb.xform[1] = p.originY;
  cb.xform[2] = p.invScale;
  cb.xform[3] = p.opacity;
  cb.misc[0] = (float)cw;
  cb.misc[1] = (float)ch;
  context->UpdateSubresource(paramBuf.Get(), 0, nullptr, &cb, 0, 0);
  return true;
}

bool DxCanvasLayer::uploadCanvas(ID3D11DeviceContext* context, bool pq) {
  const bool bDue = state.takeUploadDue();
  if (!bDue && pq == bEncodedPq) {
    return true;  // 内容与域都没变: 纹理已是最新
  }
  const int32_t cw = state.width();
  const int32_t ch = state.height();
  if (cw <= 0 || ch <= 0) {
    return false;
  }
  if (pq) {
    if (!bLutBuilt) {
      canvasPqEncodeLut(pqLut);
      bLutBuilt = true;
    }
    // CPU 侧 SDR→线性→203nits→PQ 预编码(拍板 §二.1): 每次内容变化一次,
    // 逐帧零增量; alpha 是覆盖度不过域变换
    encoded.resize((size_t)cw * ch * 4);
    const uint8_t* src = state.data();
    for (size_t i = 0; i < (size_t)cw * ch * 4; i += 4) {
      encoded[i] = pqLut[src[i]];
      encoded[i + 1] = pqLut[src[i + 1]];
      encoded[i + 2] = pqLut[src[i + 2]];
      encoded[i + 3] = src[i + 3];
    }
    context->UpdateSubresource(canvasTex.Get(), 0, nullptr, encoded.data(),
                               cw * 4, 0);
  } else {
    context->UpdateSubresource(canvasTex.Get(), 0, nullptr, state.data(),
                               cw * 4, 0);
  }
  bEncodedPq = pq;
  return true;
}

}
