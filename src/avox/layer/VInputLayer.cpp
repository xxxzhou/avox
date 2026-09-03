#include "VInputLayer.hpp"

namespace avox {

VInputLayer::VInputLayer() { cpuBuffer = std::make_unique<SwVideoBuffer>(); }

VInputLayer::~VInputLayer() {}

void VInputLayer::inputCpuData(uint8_t* data, const ImageFormat& format,
                               bool bCopyData) {
  // 通知图像变化
  if (cpuBuffer->getImageFormat() != format) {
    dispatch(&IVInputLayerOb::onImageChange, format);
    onFormatChange();
  }
  cpuBuffer->setData(data, format, bCopyData);
  bCpuInput = true;
  bDateUpdate = true;
}

void VInputLayer::inputCpuData(IImageBuffer* buffer, bool bCopyData) {
  if (!buffer) {
    return;
  }
  // 通知图像变化
  if (cpuBuffer->getImageFormat() != buffer->getImageFormat()) {
    dispatch(&IVInputLayerOb::onImageChange, buffer->getImageFormat());
    cpuBuffer->setImageFormat(buffer->getImageFormat());
    onFormatChange();
  }
  cpuBuffer->copyFrom(buffer, bCopyData);
  bCpuInput = true;
  bDateUpdate = true;
}

void VInputLayer::inputCpuData(const YUVFrame& frame, bool bCopyData) {
  ImageFormat format = {};
  yuv2ImageFormat(frame, format);
  if (cpuBuffer->getImageFormat() != format) {
    dispatch(&IVInputLayerOb::onImageChange, format);
    cpuBuffer->setImageFormat(format);
    onFormatChange();
  }
  cpuBuffer->form(frame, bCopyData);
  bCpuInput = true;
  bDateUpdate = true;
}

void VInputLayer::setLayerFormat(VLayer* layer, const ImageFormat& format) {
  ImageFormat tmpeFormat = format;
  // 输入层负责把bgra转化成rgba
  if (format.imageType == ImageType::bgra8) {
    tmpeFormat.imageType = ImageType::rgba8;
  }
  // 输入层负责把rgb转化成rgba
  if (format.imageType == ImageType::rgb8) {
    tmpeFormat.imageType = ImageType::rgba8;
  }
  layer->setInSlot(0, format);
  layer->setOutSlot(0, tmpeFormat);
  // 管线可以尝试运行
  layer->getPipeGraph()->reset();
}

void addInputeOb(IVInputLayer* layer, IVInputLayerOb* ob) {
  VInputLayer* inputLayer = dynamic_cast<VInputLayer*>(layer);
  if (inputLayer) {
    inputLayer->addObserver(ob);
  }
}

void removeInputeOb(IVInputLayer* layer, IVInputLayerOb* ob) {
  VInputLayer* inputLayer = dynamic_cast<VInputLayer*>(layer);
  if (inputLayer) {
    inputLayer->removeObserver(ob);
  }
}

}