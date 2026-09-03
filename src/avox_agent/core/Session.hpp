#pragma once

// ============================================================================
// 会话: 一次 agent 交互的 append-only 真相源。
//
// 对齐 dsh 的 packages/core/session/src/index.ts (Session 部分)。
// dsh 的 SessionStore (多会话注册表) 未整套移植: avox 的多会话并存是「宿主主会话 +
// subagent 工具栈帧内的瞬态子会话」(见 compose/Subagents), 子会话直接落同一
// sessionRoot 的目录布局, 不经中央注册表。
//
// 模型历史从本日志派生 (deriveMessages), 不单独维护。这条是整套设计的地基:
// 压缩能安全、resume 能保真、回放能自动化、UI 卡片能在回放里重现, 全部建立在
// 「模型看到的每个字节都能从日志重建」之上。
// ============================================================================

#include <cstddef>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

#include "SessionTypes.hpp"
#include "Surface.hpp"

namespace avox {

class Session;

// 会话事件观察者。
//
// 事件一旦进入日志就是已提交的: 观察者抛出的异常只被记录, 不会影响 append 的结果,
// 也不会阻止后续观察者收到通知。持久化、UI、遥测都挂在这里。
class SessionObserver {
 public:
  virtual ~SessionObserver() = default;

  // session: 产出该事件的会话 (借用引用, 回调期间有效)。
  // event: 刚提交的事件 (借用引用, 回调期间有效)。
  virtual void onSessionEvent(const Session& session, const SessionEvent& event) = 0;
};

// 判定一个事件载荷是否可上模型可见表面。
//
// append 的两个重载靠它做编译期分流: 可上表面的载荷必须带 SurfaceIntent, 不可上表面的
// 载荷则连带的机会都没有。这是 dsh types.ts 里那句「非 surface 事件永不携带 surface
// 元数据 —— 编译器在 append 调用点强制」在 C++ 里的对应物。
template <class T>
inline constexpr bool kIsSurfacePayload =
    std::is_same_v<T, UserMessageData>
    || std::is_same_v<T, AssistantMessageData>
    || std::is_same_v<T, ToolResultData>;

// 不加 AVOX_EXPORT: avox 用静态 CRT (/MT), 跨 DLL 边界传 std::string / vector 会让对象在
// 一个堆上构造、在另一个堆上析构。core 的接口大量使用标准容器, 所以它是**DLL 内部设施**;
// 跨语言边界一律走 avox_agent/export 的 C 接口 (char* + 裸指针), 那也是旧 IAgentClient
// 导出路径的做法。自测通过直接编译 core 的源文件参与, 而不是从 DLL 导入。
class Session {
 public:
  // 构造一个会话。
  //
  // seed: resume / fork / replay 进来的历史。**走与 append 完全相同的不变式校验** ——
  //   否则一个坏的 track 文件能造出任何后端都存不下的活日志: 每条事件的 seq 必须从 0
  //   连续 (全系统依赖的 seq == 下标 契约), surface 转移必须合法。校验失败抛
  //   std::runtime_error。
  // header: 存储元数据; 不给则合成一个最小 header (盖当前 SESSION_FORMAT_VERSION)。
  //
  // seed 非空且末条不是 session/end-seed 时, 构造末尾补一条 —— 于是后端捕获创建 seed
  // 时标记已在其中, 不必在加载期回写。已带该标记的 seed 不重复补: 冷会话在首次触碰时
  // 才 resume, 反复打开一个未改动的会话不能让它的日志逐次变长。
  explicit Session(SessionId id, std::vector<SessionEvent> seed = {},
                   std::optional<SessionHeader> header = std::nullopt);

  // 持有 surface 对日志的引用, 故不可拷贝不可移动。需要跨作用域传递时用 unique_ptr。
  Session(const Session&) = delete;
  Session& operator=(const Session&) = delete;
  Session(Session&&) = delete;
  Session& operator=(Session&&) = delete;

  // ---- 身份与元数据 ----

  const SessionHeader& getHeader() const { return header; }
  const SessionId& id() const { return header.id; }

  // 本进程追加的第一个 seq (无 seed 时为 0)。
  //
  // seq 更小的事件是经构造进来的 (replay / fork / resume), 从未在观察者上发布过, 所以
  // 「把重放日志当作发布替代品」的消费方 (遥测接管) 要从这里开始。
  //
  // 与 header.seedLength 的区别: 后者是持久的 fork 血缘边界; 本字段是进程内的构造事实
  // —— 一个 resume 出来的会话, 它的构造 seed 是整份存储日志, 而 header 里仍保留原始的
  // fork 值。
  size_t getFirstLiveSeq() const { return firstLiveSeq; }

  // ---- 日志 ----

  // append-only 事件日志。
  //
  // 借用引用: **在下一次 append 之前有效**。append 可能让底层容器重分配, 届时先前取得
  // 的引用与其中的元素引用一并失效。需要跨 append 持有请自行拷贝, 或改持 seq。
  const std::vector<SessionEvent>& events() const { return eventLog; }

  // 下一个事件的序号 —— 恒等于日志长度 (seq == 下标 的连续性契约)。
  size_t seq() const { return eventLog.size(); }

  // ---- 追加 ----
  //
  // 返回新事件的 seq。
  //
  // 与 dsh 的偏离: dsh 返回 logged event 对象, 因为 JS 那侧没有悬垂问题。C++ 返回 seq
  // —— 指向 vector 内部的引用会在下次 append 重分配时失效, 而调用方真正需要的就是 seq
  // (供 sourceEventSeqs 引用)。需要完整事件用 events()[seq]。
  //
  // 校验失败 (surface 元数据违规、不透明 JSON 载荷非法、发布窗口内重入) 抛
  // std::runtime_error, 且日志不发生任何变化 —— 日志是持久真相, 坏事件必须在追加现场
  // 失败, 而不是稍后在后端落盘时才失败。

  // 不可上表面的事件: 边界标记、分片、仅记日志的记录。
  template <class T>
    requires(!kIsSurfacePayload<T>)
  size_t append(T data) {
    return appendImpl(EventData{std::move(data)}, std::nullopt);
  }

  // 可上表面的事件: 必须声明它如何进入表面。
  template <class T>
    requires kIsSurfacePayload<T>
  size_t append(T data, SurfaceIntent intent) {
    return appendImpl(EventData{std::move(data)}, std::move(intent));
  }

  // ---- 投影 ----

  // 有序模型可见表面。
  SurfaceManager& getSurface() { return surfaceManager; }

  // 派生模型历史: 沿 surfaceOp 标记维护的有序表面逐节点投影。
  //
  // 表面是派生历史的唯一来源, 所以没有标记的原始事件 (分片、turn 边界) 正确地缺席,
  // 而一次压缩 replace 会把被遮蔽的节点从派生里删掉。投影规则见 deriveEventMessage。
  //
  // 已缓存: 每个表面节点只投影一次, 一次调用的成本是 O(新节点数); 一次表面重写
  // (replace, 见 SurfaceManager::replaceGeneration) 会触发整体重建。
  //
  // 借用引用: 在下一次 append 之前有效。
  const std::vector<Message>& deriveMessages();

  // 日志最后一条 header 事件之后生效的 EpochHeader —— 也就是**下一次请求要比对的那个
  // header**; 首条 header 事件之前返回 nullptr。
  //
  // 增量维护: 每条 header 事件只折叠一次, 于是每步读取的成本是 O(新事件数)。
  // 「header 不变」等价于「请求前缀字节相同」, 这是 KV cache 命中的判据。
  //
  // 借用指针: 在下一次 append 之前有效。
  const EpochHeader* requestHeader();

  // 最新的已解析路由元数据; 首条 request/context 事件之前返回 nullptr。
  const RequestContext* requestContext();

  // 当前的 todo 列表投影 —— 展示层状态, 永不在模型 surface 上 (todo/write 是仅记日志
  // 的事件)。dsh 投影语义: 最新一条 todo/write 生效 (整表快照, latest write wins);
  // turn/start 清回空 —— todo 是「当前 turn 的工作面」, 跨 turn 不保留待办。
  // 尚无有效写入 (或已被 turn 边界清空) 时返回 nullptr。
  //
  // 借用指针: 在下一次 append 之前有效。
  const std::vector<TodoItem>* todos();

  // ---- 观察者 ----

  // 借用指针, 生命周期须覆盖到本会话销毁或 removeObserver。重复添加同一指针无效。
  void addObserver(SessionObserver* ob);
  void removeObserver(SessionObserver* ob);

 private:
  size_t appendImpl(EventData data, std::optional<SurfaceIntent> intent);

  // 校验跨类型系统边界的不透明 JSON 载荷 (ToolResultData::meta、
  // EpochHeader::toolsJson)。其余字段由 C++ 类型系统保证, 无需运行时校验 ——
  // 这是 C++ 相对 dsh 省掉 snapshotJsonValue 整套递归校验的地方, 但这两个字段是
  // 字符串装的 JSON, 类型系统管不到, 而它们要落进持久日志。
  static void assertOpaqueJson(const EventData& data);

  // 命名注意: 不能叫 log —— 全项目通用的 LOGFLF 宏展开成对自由函数 log(...) 的调用,
  // 一个叫 log 的成员会把它遮蔽掉, 导致本类内所有日志宏编译失败。
  std::vector<SessionEvent> eventLog;
  // 必须在 eventLog 之后声明: 它在构造时捕获 eventLog 的引用。
  SurfaceManager surfaceManager{eventLog};

  SessionHeader header;
  size_t firstLiveSeq = 0;

  // 发布窗口标志: 观察者回调期间不得重入 append —— 重入会让 seq 与发布顺序交错,
  // 观察者看到的日志就不再是「事件提交时的那份日志」。
  bool appending = false;

  // 派生历史缓存。
  std::vector<Message> derivedCache;
  // 已消费的表面节点数 (不等于缓存条数: 空 content 的 assistant/message 不产出消息)。
  size_t derivedNodes = 0;
  size_t derivedGeneration = 0;

  // header / context / todo 折叠缓存: 记录已折叠到的日志长度。
  std::optional<EpochHeader> headerFold;
  size_t headerFoldCount = 0;
  std::optional<RequestContext> contextFold;
  size_t contextFoldCount = 0;
  std::optional<std::vector<TodoItem>> todosFold;
  size_t todosFoldCount = 0;

  std::vector<SessionObserver*> observers;
};

}
