#include "../AvoxBase.h"

#include "SubprocessRunner.hpp"

namespace avox {

// 拿全局 Python 执行器单例: SubprocessRunner (spawn 机器 python, 不嵌入 CPython, 不绑定版本)。
// 首次调用 lazy 创建 static 单例, 之后返回同一实例 (进程生命周期)。
IPyRunner* getPyRunner() {
  static SubprocessRunner runner;
  return &runner;
}

}
