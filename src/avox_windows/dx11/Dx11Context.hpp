#pragma once

#include "../../avox/AvoxLayer.h"
#include "../../avox/video/ImageBuffer.hpp"
#include "../WinCommon.hpp"
#include "Dx11Helper.hpp"

namespace avox {

class IDx11Context : public IRenderContext {
 public:
  IDx11Context() = default;
  virtual ~IDx11Context() = default;

  // IRenderContext
 public:
  virtual RenderType getRenderType() override { return RenderType::D3D11; }

 public:
  virtual ID3D11Device* getDevice() = 0;
  virtual ID3D11Texture2D* getTexture() = 0;
  // 如果能交互,自身就是Dx11SharedTex,否则是Dx11Context
  virtual bool bInteropTexture() { return false; }
};

class Dx11Context : public IDx11Context {
 public:
  Dx11Context() {};
  virtual ~Dx11Context() {};

 protected:
  ID3D11Device* device = nullptr;
  ID3D11Texture2D* texture = nullptr;
  MComPtr<ID3D11DeviceContext> d3dcontext = nullptr;

 public:
  void setDevice(ID3D11Device* device);
  void setTexture(ID3D11Texture2D* texture);
  ID3D11DeviceContext* getContext();

 public:
  virtual ID3D11Device* getDevice() override;
  virtual ID3D11Texture2D* getTexture() override;
};

bool fetchTexture(IDx11Context* context, ImageBuffer* buffer);

}