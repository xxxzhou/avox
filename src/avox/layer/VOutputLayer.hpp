#pragma once

#include "../AvoxLayer.h"
#include "../module/Observer.hpp"
#include "../video/VideoBuffer.hpp"
#include "VLayer.hpp"

namespace avox {

class VOutputLayer : public IVOutputLayer,
                     public Observer<IVOutputLayerOb>,
                     public IParamet<OutputParamet> {
 public:
  VOutputLayer();
  virtual ~VOutputLayer();

 protected:
  std::unique_ptr<ImageBuffer> cpuBuffer = nullptr;
};

}