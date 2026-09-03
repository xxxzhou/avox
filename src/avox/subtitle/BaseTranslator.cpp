#include "BaseTranslator.hpp"

#include "../module/AvoxManager.hpp"
#include "../module/ModuleMgr.hpp"

namespace avox {

void BaseTranslator::setSourceLanguage(Language lang) {
  sourceLang = lang;
  sourceLangCode = getLangCode(lang);
}

void BaseTranslator::setTargetLanguage(Language lang) {
  targetLang = lang;
  targetLangCode = getLangCode(lang);
}

const char* BaseTranslator::getLastError() const {
  return lastError.c_str();
}

const char* getLangCode(Language lang) {
  switch (lang) {
    case Language::zh:
      return "zh";
    case Language::en:
      return "en";
    case Language::ja:
      return "ja";
    default:
      return "auto";
  }
}

// 通过 AvoxManager 工厂表创建翻译器(组件 loadModule 时注册),
// none 或组件未注册返回 nullptr
ITranslator* createTranslator(TranslatorType type) {
  ModuleMgr::Get().ensureStarted();
  const char* key =
      type == TranslatorType::http ? "http" :
      type == TranslatorType::onnx ? "onnx" : nullptr;
  return key ? AvoxManager::Get().translatorHub.create(key) : nullptr;
}

}
