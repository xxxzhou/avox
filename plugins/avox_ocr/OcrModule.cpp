#include "OcrModule.hpp"

#include "TextRecognizer.hpp"
#include "avox/AvoxVision.h"
#include "avox/module/AvoxManager.hpp"

namespace avox {

bool OcrModule::loadModule(IOption* option) {
  (void)option;
  // OCR 工厂: new TextRecognizer() 在本 plugin 内(同一堆);
  // 业务经 textRecognizerHub.create("ppocr") 查表拿实例
  // (avox_ocr 不直接 link onnxruntime: TextRecognizer 经 onnxSessionHub.create("onnx") 拿 IONNXSession,
  //  DEPS avox_onnx 保证 avox_onnx 先加载注册了 onnxSessionHub; 模型首次 recognize 时按需加载, 失败降级)
  AvoxManager::Get().textRecognizerHub.reg(
      "ppocr", []() -> ITextRecognizer* { return new TextRecognizer(); });
  return true;
}

AVOX_REGISTER_MODULE(OcrModule, avox_ocr)

}
