#pragma once

#include <string>

#include "../AvoxDef.h"
#include "../AvoxBase.h"

namespace avox {

/**
 * @brief 翻译器基类
 * 实现通用的语言设置等功能
 */
class AVOX_EXPORT BaseTranslator : public ITranslator {
 public:
  BaseTranslator() = default;
  virtual ~BaseTranslator() = default;

 protected:
  Language sourceLang = Language::ja;
  Language targetLang = Language::zh;
  std::string sourceLangCode = "ja";
  std::string targetLangCode = "zh";
  std::string lastError;
  std::string resultBuffer;

 public:
  void setSourceLanguage(Language lang) override;
  void setTargetLanguage(Language lang) override;
  const char* getLastError() const override;
};

const char* getLangCode(Language lang);

}
