#pragma once

// ============================================================================
// 会话日志的 JSONL 持久化。
//
// 对应 dsh 的 packages/session/session-persistence (dsh 还有一个 SQLite 后端;
// avox 只做 JSONL —— 它同时是 resume 的种子、回放 fixture 与事后审计的载体, 一份文件
// 三种用途, 这正是把旧 TrackRecorder 的旁路记录合并进日志换来的)。
//
// 文件格式 (dsh 的 session-persistence-jsonl 布局, avox 全明文):
//   第 1 行   存储元数据 (SessionHeader)
//   第 2 行起 每行一条事件 (avox 写侧) 或一条打包存储行 (dsh 写侧), seq 从 0 连续
//
// 打包存储行 (text-chunks / reasoning-chunks / tool-call-chunks) 是 dsh 把一串连续
// 同块 delta 分片压成一行的编码, 读侧必须展开 (见 SessionPersistence.cpp); avox 不写
// 打包行 (读方 layout-blind, 合法), 但读 dsh 产的日志会撞上。zstd 压缩档 (.jsonl.zstd)
// 拒读 —— avox 只认明文。
//
// 以二进制模式打开并手写 "\n": 不让 Windows 的 CRLF 转换介入, 否则同一份 fixture 在
// 不同平台上字节不同, 跨平台回放就对不上。
// ============================================================================

#include <cstddef>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

#include "Session.hpp"
#include "SessionTypes.hpp"

namespace avox {

// 把会话事件逐行落盘的观察者。
//
// 失败姿态: 落盘是旁路, 磁盘满或权限不足绝不能让 agent 停下来。写失败只记 warn 并留在
// lastError() 里供宿主查询; Session 本身会吞掉观察者异常。
class SessionWriter : public SessionObserver {
 public:
  SessionWriter() = default;
  ~SessionWriter() override;

  SessionWriter(const SessionWriter&) = delete;
  SessionWriter& operator=(const SessionWriter&) = delete;

  // 挂到一个会话上并对齐文件与内存, 然后注册为观察者。
  //
  // 文件不存在: 写元数据行, 再写 session.events() 的全部事件。
  // 文件已存在: 数出已有的事件数 N (打包行按成员数计), 只补写 seq >= N 的部分; 尾部
  // 撕裂行 (无换行符收尾) 先截掉再续写。
  //
  // 为什么必须对齐: resume 的路径是 loadSession -> 构造 Session -> attach。前两步会在
  // 内存里产生文件中没有的事件 (给崩溃遗留 turn 补的 interrupted、构造补的
  // session/end-seed), 若不在 attach 时补齐, 下次 resume 会再补一遍, 日志里就会堆出
  // 一串重复的收尾标记。
  //
  // 返回是否成功打开; 失败原因见 lastError()。
  bool attach(Session& session, const std::string& path);

  // 注销观察者并关闭文件 (幂等)。
  void detach();

  // 把缓冲刷到磁盘。
  void flush();

  const std::string& path() const { return filePath; }
  const std::string& lastError() const { return errorText; }

  // SessionObserver
  void onSessionEvent(const Session& session, const SessionEvent& event) override;

 private:
  void writeLine(const std::string& line);

  std::ofstream file;
  std::string filePath;
  std::string errorText;
  Session* attached = nullptr;
  // 工作线程写 + 宿主线程 flush/detach 互斥。
  std::mutex mtx;
};

// 从文件加载出来的会话。
struct LoadedSession {
  SessionHeader header;
  std::vector<SessionEvent> events;
  // 因未识别类型且带 ignorable 标记而跳过的条数 (跨版本读取的正常现象)。
  size_t skippedIgnorable = 0;
  // 是否给崩溃遗留的未闭合 turn 补写了收尾标记。
  //
  // 这个标志有诊断价值: interrupted 只可能由本函数补写, 驱动自己永不产生, 所以一份
  // 日志里它的出现次数就等于崩溃次数。
  bool repairedInterruptedTail = false;
};

// 读回一份会话日志。
//
// 除了解码, 还做一件事: 给崩溃遗留的未闭合 turn (以及它里面未闭合的 step) 补写收尾标记,
// 让日志恢复结构自洽。补写的事件成为 seed 的一部分, 由 SessionWriter::attach 落盘。
//
// 文件不存在、元数据行非法、版本不符、事件 seq 不连续、或遇到未识别的必需事件时抛
// std::runtime_error —— 坏日志必须拒绝, 而不是静默恢复出一个被掏空的会话。
LoadedSession loadSession(const std::string& path);

}
