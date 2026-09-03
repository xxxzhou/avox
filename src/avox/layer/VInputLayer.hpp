#pragma once

#include "../AvoxLayer.h"
#include "../module/Observer.hpp"
#include "../video/VideoBuffer.hpp"
#include "VLayer.hpp"

namespace avox {

class VInputLayer : public IVInputLayer, public Observer<IVInputLayerOb> {
public:
  VInputLayer();
  virtual ~VInputLayer();

protected:
  std::unique_ptr<SwVideoBuffer> cpuBuffer = nullptr;
  bool bCpuInput = false;
  bool bGpuInput = false;
  bool bDateUpdate = false;

public:
  void inputCpuData(uint8_t *data, const ImageFormat &format, bool bCopyData);

public:
  virtual void inputCpuData(IImageBuffer *buffer, bool bCopyData) override;
  virtual void inputCpuData(const YUVFrame &frame, bool bCopyData) override;

protected:
  virtual void onFormatChange() = 0;

public:
  void setLayerFormat(VLayer *layer, const ImageFormat &format);
};

}