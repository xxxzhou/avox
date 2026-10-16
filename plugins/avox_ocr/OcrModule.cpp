#include "OcrModule.hpp"

#include "TextRecognizer.hpp"
#include "avox/AvoxVision.h"
#include "avox/module/AvoxManager.hpp"

namespace avox {

bool OcrModule::loadModule(IOption* option) {
  (void)option;
// OCR 工厂: new TextRecognizer() 在本 plugin 内(同一堆);
// 业务经 textRecognizerHub.create("ppocr") 查表拿实例
  AvoxManager::Get().textRecognizerHub.reg(
      "ppocr", []() -> ITextRecognizer* { return new TextRecognizer(); });
  return true;
}

AVOX_REGISTER_MODULE(OcrModule, avox_ocr)

}
