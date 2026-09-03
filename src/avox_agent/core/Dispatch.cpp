#include "Dispatch.hpp"

#include "avox/module/LogHelper.hpp"

namespace avox {

void reportDispatchError(const char* pointName, const char* detail) {
  LOGFLF(LogLevel::warn, "[dispatch] 扩展点 ", pointName, " 的监听器抛出异常: ",
         detail);
}

}
