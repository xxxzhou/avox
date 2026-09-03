#pragma once
#include <memory>
#include <string>

#include "../AvoxPlayer.h"

namespace avox {

struct OptionData {
  std::string key;
  ArgType option;
};

struct UpdateWindow {
  int32_t index = 0;
  // IWindow *window = nullptr;
};

// 类型不一定是基础类型，可能是自定义struc/class
#define AVOX_MAP_MPCOMMAND(XX)                                                  \
  XX(Open, 0, "open", std::string)                                             \
  XX(Ready, 1, "ready", void)                                                  \
  XX(Play, 2, "play", void)                                                    \
  XX(Close, 3, "close", void)                                                  \
  XX(Pause, 4, "pause", bool)                                                  \
  XX(Seek, 5, "seek", int64_t)                                                 \
  XX(Complete, 6, "complete", void)                                            \
  XX(Buffing, 7, "buffing", void)                                              \
  XX(Speed, 8, "speed", double)                                                \
  XX(Option, 9, "option", OptionData)                                          \
  XX(SetWindow, 10, "updateWindow", UpdateWindow)                              \
  XX(ResetDecode, 11, "resetDecode", bool)                                     \
  XX(ResetDecodeComplete, 12, "resetDecodeComplete", void)                     \
  XX(SyncPts, 13, "syncPts", void)   \
  XX(SetRemoteSdp, 14, "setRemoteSdp", std::string)   \
  XX(IFrameMode, 15, "iframeMode", bool)

// 命令类型枚举
enum class MPCommandType {
  none = -1,
#define XX(name, value, str, classtype) name = value,
  AVOX_MAP_MPCOMMAND(XX)
#undef XX
};

struct MPCommand {
  MPCommandType type = MPCommandType::none;
  virtual ~MPCommand() = default;
};

template <MPCommandType T> struct MPCommandDataMap {
  using Type = void;
};

// 用宏生成特化版本
#define XX(name, value, str, classtype)                                        \
  template <> struct MPCommandDataMap<MPCommandType::name> {                   \
    using DataType = classtype;                                                \
  };
AVOX_MAP_MPCOMMAND(XX)
#undef XX

using MPCommandPtr = std::shared_ptr<MPCommand>;

// 主模板（默认处理非void类型）
template <MPCommandType T,
          typename DataType = typename MPCommandDataMap<T>::DataType>
class XMPCommand : public MPCommand {
  static_assert(!std::is_void_v<DataType>,
                "This command requires non-void data");

public:
  explicit XMPCommand(const DataType &data_) : data(data_) {
    MPCommand::type = T;
  }
  virtual ~XMPCommand() {}
  const DataType &getData() const { return data; }
  DataType &getData() { return data; }
  void setData(const DataType &data_) { data = data_; }

private:
  DataType data = {};
};

// 偏特化：处理DataType为void的情况
template <MPCommandType T> class XMPCommand<T, void> : public MPCommand {
public:
  XMPCommand() { MPCommand::type = T; }
  virtual ~XMPCommand() {}
};

// 非void类型工厂函数
template <MPCommandType T>
std::enable_if_t<!std::is_void_v<typename MPCommandDataMap<T>::DataType>,
                 std::shared_ptr<XMPCommand<T>>>
createCommand(const typename MPCommandDataMap<T>::DataType &data) {
  return std::make_shared<XMPCommand<T>>(data);
}

// void类型工厂函数
template <MPCommandType T>
std::enable_if_t<std::is_void_v<typename MPCommandDataMap<T>::DataType>,
                 std::shared_ptr<XMPCommand<T>>>
createCommand() {
  return std::make_shared<XMPCommand<T>>();
}

// 获取命令的辅助函数
template <MPCommandType T>
std::shared_ptr<XMPCommand<T>> getCommand(MPCommandPtr cmd) {
  if (cmd && cmd->type == T) {
    return std::dynamic_pointer_cast<XMPCommand<T>>(cmd);
  }
  return nullptr;
}

#define XX(name, value, str, classtype)                                        \
  using name##CommandPtr = std::shared_ptr<XMPCommand<MPCommandType::name>>;
AVOX_MAP_MPCOMMAND(XX)
#undef XX

}
