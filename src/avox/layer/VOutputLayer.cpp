#include "VOutputLayer.hpp"

namespace avox {

VOutputLayer::VOutputLayer() { cpuBuffer = std::make_unique<ImageBuffer>(); }

VOutputLayer::~VOutputLayer() {}

void addOutputOb(IVOutputLayer* layer, IVOutputLayerOb* ob) {
  VOutputLayer* inputLayer = dynamic_cast<VOutputLayer*>(layer);
  if (inputLayer) {
    inputLayer->addObserver(ob);
  }
}

void removeOutputOb(IVOutputLayer* layer, IVOutputLayerOb* ob) {
  VOutputLayer* inputLayer = dynamic_cast<VOutputLayer*>(layer);
  if (inputLayer) {
    inputLayer->removeObserver(ob);
  }
}


}
