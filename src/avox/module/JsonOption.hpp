#pragma once

#include "../AvoxBase.h"
#include "Json.hpp"
#include "Observer.hpp"

namespace avox {

// 提供一种由Json实现的键值对配置
class AVOX_EXPORT JsonOption : public IOption, public Observer<IOptionOb> {
 public:
  JsonOption();
  virtual ~JsonOption() = default;

 protected:
  Json option = nullptr;

 public:
  Json& getOption() { return option; }

 public:
  virtual void onOptionChange(const char* key, ArgType option);

 public:
  virtual void setBool(const char* key, bool value) override;
  virtual void setInt(const char* key, int64_t value) override;
  virtual void setString(const char* key, const char* value) override;
  virtual void setNumber(const char* key, double value) override;
  virtual ArgType getType(const char* key) override;
  virtual int64_t getInt(const char* key) override;
  virtual double getDouble(const char* key) override;
  virtual const char* getString(const char* key) override;
  virtual bool getBool(const char* key) override;
};

class OptionLink : public IOptionOb {
 public:
  OptionLink() = default;
  virtual ~OptionLink();

 protected:
  JsonOption* option = nullptr;

 public:
  // 链到上游配置树: 摘掉旧链(重链安全), 全量重放已有键给 onOptionChange。
  // 遍历 Json 不加锁, 只能在停止态、由同一控制线程调用
  void linkOption(IOption* context);
  // 与上游解绑(析构兜底); 持有上游指针的组件(如设备源比会话活得久)必须显式调
  void unlinkOption();
  JsonOption* getLink();
};

// 配置变化传递
void optionChange(IOption* srcOption, IOption* dstOption, const char* key,
                  ArgType option);
// 把当前配置所有内容复制到目标中
void optionCopy(IOption* srcOption, IOption* dstOption);

}