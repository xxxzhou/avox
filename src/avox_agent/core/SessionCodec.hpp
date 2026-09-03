#pragma once

// ============================================================================
// 会话事件 <-> JSON 编解码。
//
// 对应 dsh 的 packages/session/session-persistence 里的编解码部分 (dsh 把它与后端实现
// 放在同一个包; 这里拆开, 因为 avox 只有一个 JSONL 后端, 而编解码还要给回放 fixture 用)。
//
// wire 字段名与 dsh 的 SessionEvent 逐字一致 (type / seq / time / data / surfaceOp /
// sourceEventSeqs / ignorable), 枚举也用 dsh 的字面量 —— 两边的日志可以直接对照, 将来
// 也能拿 dsh 录的 fixture 喂给 avox 的回放。
//
// 注意 time: C++ 结构体成员叫 timeMs (单位显式), wire 上是 dsh 的 time。
// ============================================================================

#include <string>

#include "SessionTypes.hpp"

namespace avox {

// 一条事件编成 JSONL 的一行 (不含换行符)。
std::string encodeEvent(const SessionEvent& event);

// 存储元数据编成 JSONL 的首行 (不含换行符)。
std::string encodeHeader(const SessionHeader& header);

// 解码一条事件的结果。
struct DecodedEvent {
  enum class Status {
    // 已识别并解出事件。
    Ok,
    // 类型未识别但带 ignorable 标记 —— 可以安全跳过。
    SkippedIgnorable,
  };

  Status status = Status::Ok;
  // Status::Ok 时有效。
  SessionEvent event;
};

// 解码 JSONL 的一行事件。
//
// 类型未识别且**没有** ignorable 标记时抛 std::runtime_error: 读者必须拒绝重建整个会话,
// 而不是静默丢弃 —— 一个未识别的必需事件可能改变其余日志的解释方式 (例如一个 replace,
// 丢了它就会把已被遮蔽的旧轮次重新喂给模型)。
//
// 结构非法 (缺字段、类型不符、枚举值不认识) 同样抛。
DecodedEvent decodeEvent(const std::string& line);

// 解码 JSONL 首行的存储元数据。
// 版本不等于 SESSION_FORMAT_VERSION 时抛 —— 未发布期不提供迁移, 直接拒绝。
SessionHeader decodeHeader(const std::string& line);

}
