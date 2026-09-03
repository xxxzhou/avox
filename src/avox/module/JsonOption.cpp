#include "JsonOption.hpp"

#include "LogHelper.hpp"

namespace avox {

JsonOption::JsonOption() { option = Json(ArgType::Object); }

void JsonOption::onOptionChange(const char* key, ArgType option) {}

void JsonOption::setBool(const char* key, bool value) {
  option.setBool(key, value);
  onOptionChange(key, ArgType::Boolean);
  dispatch(&IOptionOb::onOptionChange, key, ArgType::Boolean);
}

void JsonOption::setInt(const char* key, int64_t value) {
  option.setInt(key, value);
  onOptionChange(key, ArgType::Int);
  dispatch(&IOptionOb::onOptionChange, key, ArgType::Int);
}

void JsonOption::setString(const char* key, const char* value) {
  option.setString(key, value);
  onOptionChange(key, ArgType::String);
  dispatch(&IOptionOb::onOptionChange, key, ArgType::String);
}

void JsonOption::setNumber(const char* key, double value) {
  option.setNumber(key, value);
  onOptionChange(key, ArgType::Number);
  dispatch(&IOptionOb::onOptionChange, key, ArgType::Number);
}

ArgType JsonOption::getType(const char* key) { return option.getType(key); }

int64_t JsonOption::getInt(const char* key) { return option.getInt(key); }

double JsonOption::getDouble(const char* key) { return option.getDouble(key); }

const char* JsonOption::getString(const char* key) {
  return option.getString(key);
}

bool JsonOption::getBool(const char* key) { return option.getBool(key); }

OptionLink::~OptionLink() {
  if (option) {
    option->removeObserver(this);
  }
}

void OptionLink::linkOption(IOption* context) {
  option = (JsonOption*)context;
  if (option) {
    option->addObserver(this);
  }
  // 选项里的值发一次给当前对象,让对象只需要处理onOptionChange
  Json& srcJson = option->getOption();
  const Json::JsonObject* srcObject = srcJson.get<Json::JsonObject*>();
  for (auto it = srcObject->begin(); it != srcObject->end(); ++it) {
    onOptionChange(it->first.c_str(), it->second.type());
  }
}

JsonOption* OptionLink::getLink() { return option; }

void optionChange(IOption* srcOption, IOption* dstOption, const char* key,
                  ArgType option) {
  if (!srcOption || !dstOption) {
    return;
  }
  switch (option) {
    case ArgType::Boolean:
      dstOption->setBool(key, srcOption->getBool(key));
      break;
    case ArgType::Int:
      dstOption->setInt(key, srcOption->getInt(key));
      break;
    case ArgType::String:
      dstOption->setString(key, srcOption->getString(key));
      break;
    case ArgType::Number:
      dstOption->setNumber(key, srcOption->getDouble(key));
      break;
    default:
      LOGFLF(LogLevel::warn, "option type no support ");
      break;
  }
}

void optionCopy(IOption* src, IOption* dst) {
  JsonOption* srcOption = dynamic_cast<JsonOption*>(src);
  JsonOption* dstOption = dynamic_cast<JsonOption*>(dst);
  if (!srcOption || !dstOption) {
    return;
  }
  // 如果把原Json赋值给新Json
  Json& srcJson = srcOption->getOption();
  const Json::JsonObject* srcObject = srcJson.get<Json::JsonObject*>();
  for (auto it = srcObject->begin(); it != srcObject->end(); ++it) {
    switch (it->second.type()) {
      {
        case ArgType::Boolean:
          dstOption->setBool(it->first.c_str(), it->second);
          break;
        case ArgType::Int:
          dstOption->setInt(it->first.c_str(), it->second);
          break;
        case ArgType::String: {
          std::string strValue = it->second;
          dstOption->setString(it->first.c_str(), strValue.c_str());
        } break;
        case ArgType::Number:
          dstOption->setNumber(it->first.c_str(), it->second);
          break;
        default:
          LOGFLF(LogLevel::warn, "option type no support ");
          break;
      }
    }
  }
}

}
