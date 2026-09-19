#pragma once

// 断链自愈桥 (a05-T3): IOParseDav 与 DavSource 同插件内的会话注册表。
// 播放中直链失效(401/403/404/410)时, IOParseDav 用「resolve 产出本直链的
// DavSource 会话」重取 refresh 换新直链续播; 会话随 DavSource 构造/析构
// 注册/注销。仅插件内部使用, 不进公开头, 也不动引擎核心。
//
// 匹配口径: IOParseDav 手里的原始播放 URL 与 DavSource 最近一次 resolve/
// refresh 的产出原样相等(同一会话产的直链字符串不变式)。
// 线程模型: 注册表互斥; 命中后的 refresh 调用方(IOParseDav IO 线程)与
// DavSource 内部结果锁串行, 不经本锁。

#include <mutex>
#include <vector>

namespace avox {

class DavSource;

namespace davbridge {

inline std::mutex& regMutex() {
  static std::mutex m;
  return m;
}
inline std::vector<DavSource*>& reg() {
  static std::vector<DavSource*> v;
  return v;
}

inline void registerSource(DavSource* s) {
  std::lock_guard<std::mutex> lk(regMutex());
  reg().push_back(s);
}
inline void unregisterSource(DavSource* s) {
  std::lock_guard<std::mutex> lk(regMutex());
  auto& v = reg();
  for (size_t i = 0; i < v.size(); ++i) {
    if (v[i] == s) {
      v[i] = v.back();
      v.pop_back();
      break;
    }
  }
}

}  // namespace davbridge

}  // namespace avox
