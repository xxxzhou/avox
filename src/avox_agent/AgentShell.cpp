#include "AgentShell.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

#include "avox/Avox.hpp"
#include "avox/AvoxLog.h"
#include "avox/module/AssetLoader.hpp"
#include "avox/module/Json.hpp"
#include "avox/module/LogHelper.hpp"
#include "avox/module/Time.hpp"
#include "adapter/ProviderCatalog.hpp"
#include "avox_cmd/CmdHelper.hpp"
#include "compose/DiagnosticAgent.hpp"
#include "core/DshLayout.hpp"
#include "core/SessionCodec.hpp"
#include "plugins/token-meter/TokenMeter.hpp"
#include "terminal/MarkdownRenderer.hpp"

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace avox {

// ========== Shell 全局交互状态 (主循环 / 输入线程 / Ctrl+C handler 共享) ==========
// 输入线程持续读 stdin 入 inputQueue (单一 stdin 读者, 避免并发读 conhost 行编辑器冲突);
// 主循环从队列消费。生成中 generating=true 时, Ctrl+C 由 handler 直接 agent->cancel()
// (driver 收敛 → whenIdle 返回)。interruptGen 标记本轮被中断; quitShell: 空闲 Ctrl+C →
// 主循环退出; inputRunning: 输入线程已启动 (waitExit 据此选队列读 / 直接 cmdReadLine)。
struct ShellState {
  std::mutex mtx;
  std::condition_variable cv;
  std::queue<std::string> inputQueue;
  std::atomic<bool> eof{false};
  std::atomic<bool> generating{false};
  std::atomic<bool> interruptGen{false};
  std::atomic<bool> quitShell{false};
  std::atomic<bool> inputRunning{false};
  std::atomic<bool> ctrlCHit{false};   // Ctrl+C 钩子触发标记: inputThreadFn 跳过残留行
  // 当前会话的 agent (Ctrl+C handler 用它取消; 由主循环维护)。
  std::atomic<Agent*> agent{nullptr};
};
static ShellState gShell;

// 条件等待: Windows handler 可 cv.notify (即时唤醒); POSIX signal 不能 notify (非
// async-signal-safe), 用 wait_for 轻轮询查 atomic 标志 (50ms, 仅 POSIX)。
template <class Pred>
static void shellCvWait(std::unique_lock<std::mutex>& lk, Pred pred) {
#ifdef _WIN32
  gShell.cv.wait(lk, pred);
#else
  while (!pred()) gShell.cv.wait_for(lk, std::chrono::milliseconds(50));
#endif
}

// 从输入队列取一行 (替代 cmdReadLine, 不与输入线程抢 stdin)。false = 输入耗尽/quitShell。
//
// eof 只在**队列排空后**才终止主循环: 输入线程可能一口气读完全部行再置 eof (stdin 是管道
// 或文件时必然如此), 先看 eof 会把已经读进来的命令整批丢掉。quitShell 相反, 它是用户按
// Ctrl+C 要求立刻退出, 残留行必须作废。
// prompt 非空时只在真的要等 (队列已空) 才打印, 免得连续几条已缓冲的命令刷出一串空提示符。
static bool shellNextLine(std::string& out, const char* prompt = nullptr) {
  std::unique_lock<std::mutex> lk(gShell.mtx);
  if (gShell.inputQueue.empty()) {
    if (prompt != nullptr && prompt[0] != '\0') {
      printf("%s", prompt);
      fflush(stdout);
    }
    shellCvWait(lk, [] {
      return !gShell.inputQueue.empty() || gShell.eof.load() || gShell.quitShell.load();
    });
  }
  if (gShell.quitShell.load()) return false;
  if (gShell.inputQueue.empty()) return false;  // 此时必然是 eof
  out = gShell.inputQueue.front();
  gShell.inputQueue.pop();
  return true;
}

// raw line editor 默认开启
static bool gUseRawInput = true;

// 命令历史 (持久化到 logs/avox_agent_history)
static CmdHistory gCmdHistory;

// Tab 补全候选: / 命令
static std::vector<std::string> agentCompleter(const char* partial) {
  std::vector<std::string> result;
  if (!partial || partial[0] == '/') {
    static const char* kCommands[] = {"/help",   "/list",  "/ls",     "/use",
                                    "/add",    "/status", "/clear",  "/raw",
                                    "/quit",   "/exit",  "/resume", "/compact",
                                    "/events"};
    for (const char* cmd : kCommands) {
      if (!partial || strncmp(partial, cmd, strlen(partial)) == 0) {
        result.push_back(cmd);
      }
    }
  }
  return result;
}

// 读一行: 按 gUseRawInput 选 cooked / raw。
static bool shellReadLine(std::string& out) {
  if (gUseRawInput) {
    cmdRawGuard rg;
    if (!rg.entered()) return cmdReadLine(out);
    return cmdReadLineRaw(out, &gCmdHistory,
                          gShell.generating.load() ? "" : "\033[34magent>\033[0m ");
  }
  return cmdReadLine(out);
}

// ========== 输入线程 + Ctrl+C handler ==========
// 输入线程: 持续读 stdin, 整行入 inputQueue; EOF 置 eof 唤醒主循环退出。
// detach (不 join): 主循环退出时本线程可能仍阻塞在 ReadConsoleW, join 会死等。
static void inputThreadFn() {
  while (true) {
    std::string line;
    if (!shellReadLine(line)) {
      gShell.eof.store(true);
      gShell.cv.notify_all();
      return;
    }
    if (gShell.ctrlCHit.exchange(false)) {
      // Ctrl+C: 丢弃残留行, 等生成真正结束再重启 line editor (否则新 editor 仍用空 prompt,
      // 中断后 agent> 不显示)。
      while (gShell.generating.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
      }
      continue;
    }
    {
      std::lock_guard<std::mutex> lk(gShell.mtx);
      gShell.inputQueue.push(std::move(line));
    }
    gShell.cv.notify_all();
  }
}

#ifdef _WIN32
// Ctrl+C 统一处理: 生成中 → agent->cancel (driver 收敛, whenIdle 返回) + interruptGen 留标;
// 空闲 → quitShell 退出。cooked 模式由 agentCtrlHandler 调, raw 模式由 readLineRawWin
// 经 setCtrlCInterceptor 钩子调 (raw 关了 ENABLE_PROCESSED_INPUT, 收不到 CTRL_C_EVENT)。
static void requestCancelOrQuit() {
  Agent* agent = gShell.agent.load();
  LOGFLF(LogLevel::warn, "[cancel] requestCancelOrQuit: generating=",
         (int)gShell.generating.load(), " agent=", (void*)agent);
  gShell.ctrlCHit.store(true);
  if (gShell.generating.load() && agent != nullptr) {
    gShell.interruptGen.store(true);
    // 取消清空 inbox 并 abort 当前活动。第一个原因胜出, 重复按只是 no-op。
    agent->cancel(AgentCancelCause{CancelByUser{}});
    gShell.cv.notify_all();
  } else {
    gShell.quitShell.store(true);
    gShell.cv.notify_all();
  }
}

static BOOL WINAPI agentCtrlHandler(DWORD ctrl) {
  if (ctrl == CTRL_C_EVENT || ctrl == CTRL_BREAK_EVENT) {
    requestCancelOrQuit();
    return TRUE;
  }
  return FALSE;
}
#else
// POSIX signal: 仅置标志 (async-signal-safe); 主循环 shellCvWait 的 wait_for 轮询消费。
static void agentSigHandler(int) {
  if (gShell.generating.load()) gShell.interruptGen.store(true);
  else gShell.quitShell.store(true);
}
#endif

// ========== 等待指示器 ==========
// 静默达 kSilentMs (等首 token / 工具执行) 时, 在独占一行显示 计时 + 旋转词, 避免静默期
// 界面像卡死。真实输出到达即清掉该行。
//   - 流式进行中 (activeStream=true) 绝不显示: token 间短停顿不触发, 否则 \r 覆写会把
//     指示混进正在流的内容里。
//   - 指示独占一行; 首次显示先 \n 新起一行; 刷新用 \r 覆写定宽。
class ConsoleThrobber {
 public:
  void start() {
    std::lock_guard<std::mutex> lock(mtx);
    if (loopThread.joinable()) return;
    running = true;
    shown = false;
    activeStream = false;
    tick = 0;
    startTime = std::chrono::steady_clock::now();
    lastActivity = startTime;
    loopThread = std::thread([this] { loop(); });
  }

  void stop() {
    {
      std::lock_guard<std::mutex> lock(mtx);
      running = false;
    }
    cv.notify_all();
    if (loopThread.joinable()) loopThread.join();
    std::lock_guard<std::mutex> lock(mtx);
    if (shown) clearLineLocked();
    shown = false;
  }

  void onToken(const char* token) {
    if (!token) return;
    std::lock_guard<std::mutex> lock(mtx);
    if (shown) {
      clearLineLocked();
      shown = false;
    }
    activeStream = true;
    lastActivity = std::chrono::steady_clock::now();
    cmdWrite(token);   // 经写线程落屏: 框选只冻显示, 不冻本线程
  }

  bool isShown() {
    std::lock_guard<std::mutex> lock(mtx);
    return shown;
  }

  void clearLine() {
    std::lock_guard<std::mutex> lock(mtx);
    if (shown) {
      clearLineLocked();
      shown = false;
    }
  }

  void markActive() {
    std::lock_guard<std::mutex> lock(mtx);
    activeStream = true;
    lastActivity = std::chrono::steady_clock::now();
  }

 private:
  void loop() {
    static const char* kWords[] = {"thinking", "analyzing", "reasoning",
                                   "processing", "waiting"};
    const int kWordCount = 5;
    static const char kFrames[] = "|/-\\";
    while (true) {
      {
        std::unique_lock<std::mutex> lock(mtx);
        if (!running) break;
        cv.wait_for(lock, std::chrono::milliseconds(250),
                    [this] { return !running; });
        if (!running) break;
        ++tick;
        const auto now = std::chrono::steady_clock::now();
        const auto sinceMs =
            std::chrono::duration_cast<std::chrono::milliseconds>(now - lastActivity)
                .count();
        if (sinceMs >= kSilentMs) activeStream = false;
        if (activeStream || sinceMs < kSilentMs) continue;
        const auto totalSec =
            std::chrono::duration_cast<std::chrono::seconds>(now - startTime).count();
        char buf[48];
        std::snprintf(buf, sizeof(buf), "%c %s %llds", kFrames[(tick / 4) % 4],
                      kWords[(totalSec / 3) % kWordCount],
                      static_cast<long long>(totalSec));
        std::string indicator(buf);
        if (indicator.size() < static_cast<size_t>(kWidth)) {
          indicator.append(static_cast<size_t>(kWidth) - indicator.size(), ' ');
        }
        // 整行一次入队 (\r 覆写 + 内容), 避免与流式输出交错撕行
        std::string line("\r");
        if (!shown) line += '\n';
        line += indicator;
        cmdWrite(line.data(), line.size());
        shown = true;
      }
    }
  }

  void clearLineLocked() {
    std::string line("\r");
    line.append(static_cast<size_t>(kWidth), ' ');
    line += '\r';
    cmdWrite(line.data(), line.size());
  }

  static constexpr int kWidth = 24;
  static constexpr int kSilentMs = 1200;
  std::mutex mtx;
  std::condition_variable cv;
  std::thread loopThread;
  bool running = false;
  bool shown = false;
  bool activeStream = false;
  long long tick = 0;
  std::chrono::steady_clock::time_point startTime;
  std::chrono::steady_clock::time_point lastActivity;
};

// ========== 会话事件观察者: 渲染流式输出与工具卡片 ==========
//
// 直接吃 SessionEvent 而不是经导出层的 JSON 回调: shell 与 core 在同一个 DLL 里, 类型
// 安全且零序列化成本。跨语言的消费者才走 export/ 的 C 接口。
//
// 一条重要契约: 本回调运行在 agent 的会话临界区内, **不得回调进 agent 的方法** (会死锁)。
// 所以这里只做输出, 取消之类的动作经 gShell 标志转交主循环或 Ctrl+C handler。
class ShellSessionObserver : public SessionObserver {
 public:
  void onSessionEvent(const Session& session, const SessionEvent& event) override {
    (void)session;
    switch (event.type) {
      case EventType::AssistantChunk:
        onChunk(std::get<AssistantChunkData>(event.data).chunk);
        break;

      case EventType::ToolResult:
        onToolResult(std::get<ToolResultData>(event.data));
        break;

      case EventType::UserMessageEvent: {
        // 插件注入的上下文 (运行时状态、循环提醒、审批通知) 也给用户看一眼 —— 它们进了
        // 模型的历史, 用户看不到会觉得模型的反应莫名其妙。
        const UserMessage& message =
            std::get<UserMessageData>(event.data).message;
        if (message.source.kind != MessageSourceKind::Plugin) break;
        std::string text;
        for (const ContentBlock& block : message.content) {
          if (const auto* t = std::get_if<TextBlock>(&block)) text += t->text;
        }
        if (text.empty()) break;
        std::lock_guard<std::mutex> lock(renderMtx);
        throbber.clearLine();
        // dsh 的 plugin source 把插件名放 plugin 字段 (name 属于 skill-invocation)。
        cmdWriteF("\n%s(%s) %s%s\n", cmdColor::kGray,
                  message.source.plugin.value_or("plugin").c_str(),
                  text.c_str(), cmdColor::kReset);
        break;
      }

      case EventType::StepEnd: {
        // 一步结束: 刷出 markdown 残留 (未闭合代码块等), 让工具卡片从干净的行开始。
        std::lock_guard<std::mutex> lock(renderMtx);
        renderer.flush();
        drainRenderer();
        break;
      }

      case EventType::TurnEnd:
        onTurnEnd(std::get<TurnEndData>(event.data));
        break;

      default:
        break;
    }
  }

  // 一轮开始前重置。
  void beginTurn() {
    std::lock_guard<std::mutex> lock(renderMtx);
    shownToolName.clear();
    turnFailure.clear();
    hitKeyboardInterrupt = false;
    throbber.start();
  }

  void endTurn() { throbber.stop(); }

  // 最近结束的一轮 (turn), 供状态栏取「当前轮」用量。未结束过任何轮时为 0。
  int lastTurn() const { return lastEndedTurn; }

  const std::string& failure() const { return turnFailure; }
  bool sawKeyboardInterrupt() const { return hitKeyboardInterrupt; }

 private:
  // 流式渲染只关心 delta: block 边界/usage/finish 是结构信息, 不上屏。
  void onChunk(const StreamChunk& chunk) {
    std::lock_guard<std::mutex> lock(renderMtx);
    if (const auto* delta = std::get_if<StreamTextDelta>(&chunk)) {
      throbber.clearLine();
      throbber.markActive();
      renderer.render(delta->text.c_str());
      drainRenderer();
      return;
    }
    if (const auto* thinking = std::get_if<StreamReasoningDelta>(&chunk)) {
      // 推理流实时打印但不进 markdown 渲染器 (它不是回复正文)。
      throbber.onToken(thinking->text.c_str());
      return;
    }
    if (const auto* call = std::get_if<StreamToolCallDelta>(&chunk)) {
      if (call->name.has_value() && !call->name->empty()
          && shownToolName != *call->name) {
        shownToolName = *call->name;
        throbber.clearLine();
        renderer.flush();
        drainRenderer();
        cmdWriteF("\n%s[%s]%s ", cmdColor::kYellow, shownToolName.c_str(),
                  cmdColor::kReset);
      }
      if (!call->argumentsDelta.empty()) {
        throbber.markActive();
        cmdWrite(call->argumentsDelta.data(), call->argumentsDelta.size());
      }
      return;
    }
  }

  // renderer 只往 pending 累积渲染产出, 这里整段入队落屏并清空 (行不被撕开)
  void drainRenderer() {
    if (renderer.pending.empty()) return;
    cmdWrite(renderer.pending.data(), renderer.pending.size());
    renderer.pending.clear();
  }

  void onToolResult(const ToolResultData& data) {
    std::string text;
    bool isError = false;
    // 消息 content 是 ToolResultBlock 列表 (恰好一个), 块内才是正文条目。
    for (const ToolResultBlock& block : data.message.content) {
      isError = isError || block.isError;
      for (const ToolResultContent& item : block.content) {
        if (const auto* t = std::get_if<TextBlock>(&item)) text += t->text;
      }
    }

    // python 子进程被 Ctrl+C 打断的兜底: console handler 可能被子进程抢先消费, 于是
    // 这里从结果里认出它, 交主循环按中断处理。
    if (text.find("KeyboardInterrupt") != std::string::npos) {
      hitKeyboardInterrupt = true;
      gShell.interruptGen.store(true);
    }

    std::lock_guard<std::mutex> lock(renderMtx);
    throbber.clearLine();
    // 结果按成败着色; 展示时截断 (完整内容在会话日志里, 溢出裁剪也已经处理过模型面)。
    constexpr size_t kMaxShow = 4096;
    std::string shown = text;
    if (shown.size() > kMaxShow) {
      shown.resize(kMaxShow);
      shown += "\n... (展示截断, 完整内容见会话日志)";
    }
    const char* color = isError ? cmdColor::kRed : cmdColor::kGray;
    cmdWriteF("\n  %s→ %s%s\n", color, shown.c_str(), cmdColor::kReset);
    shownToolName.clear();
  }

  void onTurnEnd(const TurnEndData& data) {
    std::lock_guard<std::mutex> lock(renderMtx);
    renderer.flush();
    drainRenderer();
    if (const auto* failed = std::get_if<TurnEndError>(&data.reason)) {
      turnFailure = failed->error.message;
      if (!failed->error.code.empty()) {
        turnFailure += " [" + failed->error.code + "]";
      }
    }
    lastEndedTurn = data.turn;
    cmdWrite("\n");
  }

  ConsoleThrobber throbber;
  MarkdownRenderer renderer;
  // 渲染是串行的: 分片、工具卡片、结果都可能从 driver 线程到达, 而它们共用一行输出。
  std::mutex renderMtx;
  std::string shownToolName;
  std::string turnFailure;
  bool hitKeyboardInterrupt = false;
  int lastEndedTurn = 0;
};

// ========== 帮助 / 状态打印 ==========

static void printHelp() {
  printf(
      "命令:\n"
      "  /help              显示本帮助\n"
      "  /list | /ls        列出 config/providers.json 里可用厂商/模型\n"
      "  /use <编号|厂商[/模型]>  切换 agent.json 的 provider/model (按 /list 编号或直接指定)\n"
      "  /add               新增一个厂商/模型到 config/providers.json\n"
      "  /resume [file]     从会话日志恢复 (含完整工具历史)\n"
      "  /status            显示当前配置与会话状态\n"
      "  /clear             开一个新会话并清屏\n"
      "  /compact           立刻压缩当前会话历史\n"
      "  /events [n]        打印最近 n 条会话事件 (默认 20)\n"
      "  /raw               切换输入模式 (cooked/raw)\n"
      "  /quit | quit       退出\n"
      "其它文本直接作为提问发送 (多轮上下文, 流式输出)\n");
}

// ========== 交互配置向导 ==========

static void waitExit() {
  cmdWriteFlush();  // 先把队列里的收尾内容落屏
  printf("\n按回车键退出...\n");
  fflush(stdout);
  std::string tmp;
  if (gShell.inputRunning.load()) shellNextLine(tmp);
  else cmdReadLine(tmp);
}

static std::string prompt(const char* label, const std::string& def) {
  // 向导期间 raw 行编辑器不显示默认 "agent> " (背景输入线程的编辑器渲染时查覆盖),
  // 只显示上方已打印的标签行 + 输入内容。
  setRawLinePromptOverride("");
  printf("%s", label);
  if (!def.empty()) printf(" [默认: %s]", def.c_str());
  // 末尾换行: raw 行编辑器渲染输入行时 (agent> ) 会清掉当前行, 换行让标签独占上一行
  // 不被覆盖, 输入内容落在下一行。
  printf(": \n");
  fflush(stdout);
  std::string line;
  shellNextLine(line);
  while (!line.empty() && (line.back() == '\r' || line.back() == '\n'
                           || line.back() == ' ' || line.back() == '\t')) {
    line.pop_back();
  }
  return line.empty() ? def : line;
}

// 当前 agent.json 单一平铺: 顶层即 AgentConfig, 端点细节由 providers.json 提供。
static Json loadAgentJsonRoot() {
  const auto existing = AssetLoader::loadToMemory("config/agent.json");
  if (existing.empty()) return Json(Json::JsonObject{});
  try {
    return parserJson(std::string(existing.begin(), existing.end()).c_str());
  } catch (...) {
    return Json(Json::JsonObject{});
  }
}

static bool saveAgentJsonRoot(const Json& root) {
  const std::string text = root.dump(2);
  const std::vector<uint8_t> data(text.begin(), text.end());
  return AssetLoader::saveToFile("config/agent.json", data);
}

// 生成一份最小可用的顶层平铺 agent.json (端点细节由 providers.json 提供)。
static Json flatAgentTemplate(const std::string& provider, const std::string& model) {
  Json root(Json::JsonObject{});
  root["provider"] = provider;
  root["model"] = model;
  root["maxTokens"] = 16384;
  // 推理等级默认 medium: 与 DSH 默认一致, 兼容绝大多数推理模型 (o3/o4-mini/gpt-5/r1),
  // 非推理模型 (GPT-4o/DeepSeek V3) 可在 provider settings 或 agent.json 改成 "" 不发。
  root["reasoningEffort"] = "medium";
  root["maxParallelToolCalls"] = 1;
  Json agentInstructions(Json::JsonObject{});
  agentInstructions["enabled"] = true;
  root["agentInstructions"] = agentInstructions;
  return root;
}

static bool interactiveNewConfig() {
  printf("\n未找到 assets/config/agent.json, 引导新建一个配置 (直接回车留空, 稍后用 /use 选):\n");
  printf("（provider 对应用于 providers.json 里预置的厂商名, 端点/密钥在 provider 下配置）\n\n");
  const std::string provider = prompt("provider", "");
  const std::string model = prompt(" model", "");
  const Json root = flatAgentTemplate(provider, model);
  if (!saveAgentJsonRoot(root)) {
    fprintf(stderr, "写入 assets/config/agent.json 失败 (请检查目录权限)\n");
    return false;
  }
  printf("\n已保存到 assets/config/agent.json (之后可用文本编辑器修改具体参数)\n");
  return true;
}

// ========== provider / agent 两个配置文件的管理 (/list /use /add) ==========
//
// 模型选择 (agent.json 顶层 provider/model) 与端点细节 (providers.json) 分离。
// 这几个命令就针对这两个文件: /list 枚举、/use 改 agent.json 的 provider/model、
// /add 往 providers.json 增厂商。

static Json loadProvidersRoot() {
  const auto raw = AssetLoader::loadToMemory("config/providers.json");
  if (raw.empty()) return Json(Json::JsonObject{});
  try {
    return parserJson(std::string(raw.begin(), raw.end()).c_str());
  } catch (...) {
    return Json(Json::JsonObject{});
  }
}

static bool saveProvidersJson(const Json& root) {
  const std::string text = root.dump(2);
  const std::vector<uint8_t> data(text.begin(), text.end());
  return AssetLoader::saveToFile("config/providers.json", data);
}

// providers.json 缺失时, 把内建免费厂商目录落盘为两层结构
// { "providers": { "<厂商>": { apiUrl, apiPath, apiKey, free, models: { "<模型>": {...} } } } },
// 保证 launch 后 /list、/use 与端点解析 (DeploymentLoader) 都有据可依。
static void bootstrapProvidersJson() {
  Json root(Json::JsonObject{});
  Json provs(Json::JsonObject{});
  // 扁平目录 (defaultFreeProviders 每条=一个厂商+单一模型) 按厂商名分组,
  // 归组为两层 {"provider": {apiUrl, apiKey, free, models:{...}}}。
  // 端点路径随模型不同 (对话/视觉走 chat/completions, 生图走 images/generations),
  // 所以厂商级默认取对话路径, 其它路径写进模型级覆盖——否则扁平条相互覆盖,
  // 会把整厂商的对话模型都指向生图端点 (见 cogview)。
  std::vector<std::string> vendors;
  std::vector<std::vector<ProviderEntry>> pool;
  for (const ProviderEntry& entry : defaultFreeProviders()) {
    const auto it = std::find(vendors.begin(), vendors.end(), entry.provider);
    if (it == vendors.end()) {
      vendors.push_back(entry.provider);
      pool.push_back(std::vector<ProviderEntry>{entry});
    } else {
      pool[static_cast<size_t>(it - vendors.begin())].push_back(entry);
    }
  }
  for (size_t i = 0; i < vendors.size(); ++i) {
    const std::vector<ProviderEntry>& group = pool[i];
    const ProviderEntry& base = group.front();
    // 该厂商是否全为文生图 (无对话/视觉): 是则厂商级走生图路径, 否则对话路径。
    bool allImageGen = true;
    for (const ProviderEntry& e : group) {
      if (e.apiPath.find("generations") == std::string::npos) {
        allImageGen = false;
        break;
      }
    }
    const std::string vendorPath =
        allImageGen ? base.apiPath : std::string("/chat/completions");
    Json vendor(Json::JsonObject{});
    vendor["apiUrl"] = base.apiUrl;
    vendor["apiPath"] = vendorPath;
    vendor["apiKey"] = base.apiKey;
    vendor["free"] = base.free;
    Json models(Json::JsonObject{});
    for (const ProviderEntry& entry : group) {
      Json modelNode(Json::JsonObject{});
      if (entry.chat) modelNode["chat"] = true;
      if (entry.imageInput) modelNode["imageInput"] = true;
      if (entry.imageOutput) modelNode["imageOutput"] = true;
      modelNode["multiImage"] = entry.multiImage;
      if (entry.contextWindow > 0) modelNode["contextWindow"] = entry.contextWindow;
      // 与厂商默认不同的端点路径写进模型级覆盖 (cogview 生图)。
      if (!entry.apiPath.empty() && entry.apiPath != vendorPath)
        modelNode["apiPath"] = entry.apiPath;
      models[entry.model] = modelNode;
    }
    vendor["models"] = models;
    provs[vendors[i]] = vendor;
  }
  root["providers"] = provs;
  if (!saveProvidersJson(root)) {
    fprintf(stderr, "写入 config/providers.json 失败 (请检查目录权限)\n");
  }
}

// 枚举可用厂商/模型条目: 优先 providers.json, 缺失/为空则回退内建免费目录。
//
// 只返回能当对话模型用的条目 (chat=true)。/list 与 /use 选的是「对话后端」, 纯视觉
// (如 anionex/gemini, 其端点要求每次请求都带图) 与纯生图 (cogview) 不该混进来当对话用——
// 它们仍由 vision-toolkit 作视觉工具供应商, 只是不进入对话候选列表。
static std::vector<ProviderEntry> listCatalogEntries() {
  std::vector<ProviderEntry> all;
  Json root = loadProvidersRoot();
  if (root.bObject()) {
    ProviderCatalog catalog;
    catalog.load(root);
    if (catalog.hasProviders()) {
      all = catalog.entries();
    } else {
      all = defaultFreeProviders();
    }
  } else {
    all = defaultFreeProviders();
  }
  std::vector<ProviderEntry> chatOnly;
  for (const ProviderEntry& entry : all) {
    if (entry.chat) chatOnly.push_back(entry);
  }
  return chatOnly;
}

// 是否存在可用的对话大模型: providers.json 里至少一个 chat 模型, 且 apiKey 已就绪
// (非占位符)。只带占位密钥的免费模型 (YOUR_* 等, 需用户另行申请) 不算可用 —— 这类
// 模型虽然标记为 free, 但没有有效密钥时根本发不出请求。
static bool hasUsableConversationModel() {
  Json root = loadProvidersRoot();
  if (!root.bObject()) return false;
  ProviderCatalog catalog;
  catalog.load(root);
  if (!catalog.hasProviders()) return false;
  for (const ProviderEntry& entry : catalog.entries()) {
    if (entry.chat && entry.ready()) return true;
  }
  return false;
}

// 没有任何可用的对话大模型时, 引导用户只输入 DeepSeek API Key, 自动把其余参数配好:
// provider=deepseek, apiUrl=https://api.deepseek.com, apiPath=/chat/completions,
// 模型默认 deepseek-v4-flash。providers.json 与 agent.json 一并落盘。回车跳过。
static bool interactiveDeepSeekSetup() {
  printf("\n当前没有任何可用的对话大模型 (免费模型也需配有效 API Key 才算可用)。\n");
  printf("最快接入 DeepSeek: 只输入 API Key, 其余参数自动设置, 默认模型 deepseek-v4-flash。\n");
  printf("  API Key 申请: https://platform.deepseek.com\n\n");
  const std::string apiKey = prompt("请输入 DeepSeek API Key (直接回车跳过)", "");
  if (apiKey.empty()) {
    printf("(已跳过 DeepSeek 配置)\n");
    return false;
  }

  // providers.json: 写入 deepseek 厂商节点 (保留已存在的其它厂商)。
  {
    Json root = loadProvidersRoot();
    if (!root.bObject()) root = Json(Json::JsonObject{});
    Json provs = root.value("providers", Json(Json::JsonObject{}));
    Json vendor = provs.value("deepseek", Json(Json::JsonObject{}));
    vendor["apiUrl"] = "https://api.deepseek.com";
    vendor["apiPath"] = "/chat/completions";
    vendor["apiKey"] = apiKey;
    Json models = vendor.value("models", Json(Json::JsonObject{}));
    Json modelNode(Json::JsonObject{});
    modelNode["chat"] = true;
    models["deepseek-v4-flash"] = modelNode;
    vendor["models"] = models;
    provs["deepseek"] = vendor;
    root["providers"] = provs;
    if (!saveProvidersJson(root)) {
      fprintf(stderr, "写入 config/providers.json 失败 (请检查目录权限)\n");
      return false;
    }
  }

  // agent.json: 平铺顶层, provider/model + 默认参数。
  {
    Json root(Json::JsonObject{});
    root["provider"] = "deepseek";
    root["model"] = "deepseek-v4-flash";
    root["maxTokens"] = 16384;
    // 推理等级默认 medium: 与 DSH 默认一致, DeepSeek R1/V3.2-exp 兼容档位。
    root["reasoningEffort"] = "medium";
    root["maxParallelToolCalls"] = 5;
    Json agentInstructions(Json::JsonObject{});
    agentInstructions["enabled"] = true;
    root["agentInstructions"] = agentInstructions;
    if (!saveAgentJsonRoot(root)) {
      fprintf(stderr, "写入 config/agent.json 失败 (请检查目录权限)\n");
      return false;
    }
  }

  printf("\n已配置 DeepSeek: provider=deepseek, model=deepseek-v4-flash\n");
  return true;
}

// ========== Shell 运行时 ==========
//
// 装配本身走 composeDiagnosticAgent (与导出层同一个入口); 本结构只额外持有 shell 关心的
// 两样东西: 当前 agent 与观察者。
//
// 切配置 = 整体重建。理由: 策略在安装时捕获了 llm 的引用 (压缩要发摘要请求), 替换 llm
// 对象会留下悬垂引用; 而重建一次的成本只是几个注册, 远小于一次模型往返。
struct ShellRuntime {
  ComposedAgent composed;
  ShellSessionObserver* observer = nullptr;
  Agent* agent = nullptr;

  void reset() {
    if (agent != nullptr && observer != nullptr) {
      agent->session().removeObserver(observer);
    }
    agent = nullptr;
    gShell.agent.store(nullptr);
    composed.reset();
  }

  AgentHost* host() { return composed.host.get(); }
  const AgentDeployment& deployment() const { return composed.deployment; }
};

// 装配一份运行时并打开会话。
static bool buildRuntime(ShellRuntime& runtime,
                        ShellSessionObserver* observer,
                        const std::string& sessionId) {
  runtime.reset();
  runtime.observer = observer;

  const auto raw = AssetLoader::loadToMemory("config/agent.json");
  if (raw.empty()) {
    fprintf(stderr, "读取 assets/config/agent.json 失败\n");
    return false;
  }
  const std::string agentJson(raw.begin(), raw.end());

  std::string error;
  if (!composeDiagnosticAgent(runtime.composed, agentJson, getAvoxPath() + "/logs",
                              error)) {
    fprintf(stderr, "%s\n", error.c_str());
    return false;
  }

  try {
    runtime.agent = runtime.host()->openAgent(sessionId);
  } catch (const std::exception& e) {
    fprintf(stderr, "打开会话失败: %s\n", e.what());
    runtime.reset();
    return false;
  }
  if (runtime.agent == nullptr) {
    fprintf(stderr, "打开会话失败\n");
    runtime.reset();
    return false;
  }
  runtime.agent->session().addObserver(observer);
  gShell.agent.store(runtime.agent);
  return true;
}

// 把 agent.json 的 provider/model 切到目标厂商/模型, 保存并按新配置重建运行时。
static bool applyDeploymentSelection(ShellRuntime& runtime,
                                     ShellSessionObserver* observer,
                                     const std::string& provider,
                                     const std::string& model) {
  Json root = loadAgentJsonRoot();
  root["provider"] = provider;
  root["model"] = model;
  if (!saveAgentJsonRoot(root)) {
    fprintf(stderr, "写入 assets/config/agent.json 失败\n");
    return false;
  }
  if (!buildRuntime(runtime, observer, "")) return false;
  printf("已切换到: %s / %s (开了新会话)\n", provider.c_str(), model.c_str());
  return true;
}

// 交互式往 providers.json 新增一个厂商 (含一个默认 chat 模型)。
static void interactiveAddProvider() {
  printf("新增供应商到 config/providers.json:\n");
  const std::string vendor = prompt("  厂商名", "zhipu");
  const std::string model = prompt("  模型名", "glm-4.7-flash");
  const std::string url = prompt("  apiUrl", "https://open.bigmodel.cn/api/paas/v4");
  const std::string apiPath = prompt("  apiPath", "/chat/completions");
  const std::string apiKey = prompt("  apiKey", "YOUR_API_KEY");

  Json root = loadProvidersRoot();
  if (!root.bObject()) return;
  Json provs = root.value("providers", Json(Json::JsonObject{}));
  Json vendorNode = provs.value(vendor.c_str(), Json(Json::JsonObject{}));
  vendorNode["apiUrl"] = url;
  vendorNode["apiPath"] = apiPath;
  vendorNode["apiKey"] = apiKey;
  Json models = vendorNode.value("models", Json(Json::JsonObject{}));
  Json modelNode(Json::JsonObject{});
  modelNode["chat"] = true;
  models[model] = modelNode;
  vendorNode["models"] = models;
  provs[vendor] = vendorNode;
  root["providers"] = provs;
  if (!saveProvidersJson(root)) {
    fprintf(stderr, "写入 config/providers.json 失败\n");
    return;
  }
  printf("已写入 config/providers.json; 用 /use %s/%s 切换\n", vendor.c_str(),
         model.c_str());
  fflush(stdout);
}

static void printStatus(ShellRuntime& runtime) {
  if (runtime.composed.host == nullptr) {
    printf("当前: agent 未就绪 (配置装载失败)。\n");
    printf("  用 /list 查看可用厂商, /add 新增, /use 切换配置。\n");
    return;
  }
  const AgentDeployment& deployment = runtime.deployment();
  printf("当前配置:\n");
  printf("  provider : %s\n", deployment.llm.providerName.c_str());
  printf("  apiUrl   : %s\n", deployment.llm.apiUrl.c_str());
  printf("  model    : %s\n", deployment.llm.model.c_str());
  if (runtime.composed.pool != nullptr) {
    printf("  模型池   : %zu 个免费模型可用\n", runtime.composed.pool->count());
  }
  printf("会话:\n");
  printf("  日志     : %s\n", runtime.host()->sessionPath().c_str());
  if (runtime.agent != nullptr) {
    printf("  事件数   : %zu\n", runtime.agent->session().events().size());
    printf("  历史条数 : %zu\n", runtime.agent->session().deriveMessages().size());
  }
  printf("策略:\n");
  const AgentConfig& config = deployment.agent;
  printf("  超时     : %s\n", config.enableTimeout ? "开" : "关");
  printf("  循环卫生 : %s\n", config.enableRepeatGuard ? "开" : "关");
  printf("  溢出裁剪 : %s (%d 字节)\n", config.enableSpill ? "开" : "关",
         config.spill.maxInlineBytes);
  printf("  审批     : %s\n", config.enableApproval ? "开" : "关");
  printf("  压缩     : %s\n", config.enableCompaction ? "开" : "关");
  printf("  模型路由 : %s\n", config.enableModelRoute ? "开" : "关");
  printf("  (可在 assets/config/agent.json 修改具体参数)\n");
}

// 打印最近若干条会话事件 (排查用: 直接看驱动实际写了什么)。
static void printEvents(ShellRuntime& runtime, size_t count) {
  if (runtime.agent == nullptr) return;
  const std::vector<SessionEvent>& events = runtime.agent->session().events();
  const size_t begin = events.size() > count ? events.size() - count : 0;
  printf("会话事件 (%zu / %zu):\n", events.size() - begin, events.size());
  for (size_t i = begin; i < events.size(); ++i) {
    const SessionEvent& event = events[i];
    printf("  #%zu %s\n", event.seq, eventTypeName(event.type));
  }
}

// 打印积分状态栏 (Claude Code 风格, 显示在当前输入行下方), 分两组:
//   [当前] 本轮 (turn) 的 输入/缓存/输出/积分;  [总] 当前会话累计的 输入/缓存/输出/积分。
// token-meter 关闭时不显示。
static void printCreditBar(Agent* agent, const AgentConfig& config, int turn) {
  if (agent == nullptr || !config.enableTokenMeter) return;
  const TurnCredit current =
      aggregateTurnCredits(agent->session(), turn, config.tokenMeter);
  const TurnCredit total =
      aggregateSessionCredits(agent->session(), config.tokenMeter);
  cmdWriteF("%s[当前]%s 输入 %lld / 缓存 %lld / 输出 %lld / 积分 %.2f    %s[总]%s 输入 %lld / 缓存 %lld / 输出 %lld / 积分 %.2f\n",
            cmdColor::kGray, cmdColor::kReset,
            static_cast<long long>(current.inputTokens),
            static_cast<long long>(current.cachedInputTokens),
            static_cast<long long>(current.outputTokens), current.credits,
            cmdColor::kGray, cmdColor::kReset,
            static_cast<long long>(total.inputTokens),
            static_cast<long long>(total.cachedInputTokens),
            static_cast<long long>(total.outputTokens), total.credits);
}

// ========== AgentShell::run ==========

int AgentShell::run() {
  // AVOX_AGENT_ALLOW_NONTTY: 允许 stdin 被重定向时仍然进主循环, 用于把一串命令喂进来做
  // 冒烟回归 (装配 → 斜杠命令 → 一轮真实对话 → /quit 收敛)。默认仍然拒绝: 非 tty 下
  // raw line editor 进不去, 补全与历史都失效, 交互体验会让人以为是 bug。
  const char* allowNonTty = std::getenv("AVOX_AGENT_ALLOW_NONTTY");
  const bool forceRun = allowNonTty != nullptr && allowNonTty[0] != '\0'
                        && std::strcmp(allowNonTty, "0") != 0;
  if (!cmdIsInteractive() && !forceRun) {
    fprintf(stderr,
            "avox_agent 需交互式终端 (检测到 stdin 被重定向), 请双击运行或在终端启动\n");
    return 1;
  }
  if (forceRun) gUseRawInput = false;
#ifdef _WIN32
  cmdEnableVt();
  cmdRegisterCompleter(agentCompleter);
  gCmdHistory.load(getAvoxPath() + "/logs/avox_agent_history");
  ensurePythonPath();
  SetConsoleCtrlHandler(agentCtrlHandler, TRUE);
  setCtrlCInterceptor(requestCancelOrQuit);
#else
  std::signal(SIGINT, agentSigHandler);
#endif
  // 控制台写线程: 回复期全部 shell 输出经它落屏 — conhost 的 QuickEdit 框选/点击会
  // 冻结本进程的控制台写, 走写线程后冻住的只是显示 (队列缓冲, 松开即追平), agent 的
  // driver 回调/spinner/主循环照常推进, QuickEdit 因此可以全程保持可用。
  cmdWriterStart();
  // 排空异步日志队列, 让 avox 初始化阶段的少量诊断日志先打到屏幕。
  flushLog();
  // 静默后续 avox 日志: 与 avox_cmd Shell.cpp 同一手法。buildRuntime → composeDiagnosticAgent
  // 会触发 SkillRegistry (装载 skill) / BuiltinTools (注册 read_image 等) 的 info 级
  // LOGFLF, 这些是开发期诊断, 不该漏到终端污染输入提示符与流式输出。run 内不再切到
  // 其它 observer (avox_cmd 的 cmdPlay/cmdRecord 会临时接管, 这里不需要), 退出前还原即可。
  SilentLogOb silentOb;
  setLogObserver(&silentOb);

  // 启动输入线程: 向导 (DeepSeek API Key / 新建配置) 都依赖它喂行, 必须先于向导启动。
  gShell.inputRunning.store(true);
  std::thread(inputThreadFn).detach();

  // providers.json 缺失时自动落盘内建免费厂商目录, 保证 /list /use 与端点解析有据可依。
  if (AssetLoader::loadToMemory("config/providers.json").empty()) {
    bootstrapProvidersJson();
  }

  // 没有任何可用的对话大模型 (只带占位密钥的免费模型不算可用) 时, 引导用户只输入
  // DeepSeek API Key, 自动把其余参数配好 (默认模型 deepseek-v4-flash)。
  // 非交互管道 (AVOX_AGENT_ALLOW_NONTTY 冒烟回归) 不弹向导, 保持原行为。
  if (!forceRun && !hasUsableConversationModel()) {
    interactiveDeepSeekSetup();
  }

  // 无配置: 引导新建 (DeepSeek 向导被跳过、且缺 agent.json 时仍需要)。
  if (AssetLoader::loadToMemory("config/agent.json").empty()) {
    if (!interactiveNewConfig()) {
      waitExit();
      cmdWriterStop();
      return 1;
    }
  }

  // 向导结束: 恢复 raw 行编辑器的默认 "agent> " 提示符 (向导期间的空白覆盖失效)。
  clearRawLinePromptOverride();

  ShellSessionObserver observer;
  ShellRuntime runtime;
  const bool builtOk = buildRuntime(runtime, &observer, "");
  cmdClearScreen();
  // 配置装载失败 (比如 agent.json 引用的提供商/模型在 providers.json 里没有) 不是死路:
  // 降级进「修复模式」, 仍可 /list、/add、/use 把配置补对, 完成后再正常对话。
  if (!builtOk) {
    printf("=== avox_agent — Agent 对话 Shell (修复模式) ===\n");
    printf("\n配置装载失败, agent 尚未就绪。用以下命令修复:\n");
    printf("  /list          查看可用厂商/模型\n");
    printf("  /add           往 config/providers.json 新增厂商\n");
    printf("  /use <编号|厂商[/模型]>  切换 agent.json 的 provider/model\n\n");
  } else {
    printf("=== avox_agent — Agent 对话 Shell ===\n");
    printStatus(runtime);
    printf("\n输入问题开始对话, 输入 /help 查看命令, /quit 退出\n\n");
  }

  const std::string agentPrompt =
      std::string(cmdColor::kBlue) + "agent>" + cmdColor::kReset + " ";
  while (true) {
    std::string line;
    if (!shellNextLine(line, agentPrompt.c_str())) break;
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n'
                             || line.back() == ' ' || line.back() == '\t')) {
      line.pop_back();
    }
    if (line.empty()) continue;

    if (line == "quit" || line == "exit" || line == "/quit" || line == "/exit") break;
    if (line == "/help" || line == "help") {
      printHelp();
      continue;
    }
    if (line == "/status") {
      printStatus(runtime);
      continue;
    }
    if (line == "/list" || line == "/ls") {
      const std::vector<ProviderEntry> entries = listCatalogEntries();
      if (entries.empty()) {
        printf("(无可用厂商, 用 /add 新增)\n");
        continue;
      }
      for (size_t i = 0; i < entries.size(); ++i) {
        const ProviderEntry& e = entries[i];
        std::string abilities;
        if (e.chat) abilities += "对话 ";
        if (e.imageInput) abilities += "视觉 ";
        if (e.imageOutput) abilities += "生图 ";
        printf("  %zu. %s / %s  %s%s%s\n", i + 1, e.provider.c_str(),
               e.model.c_str(), e.free ? "(免费) " : "", abilities.c_str(),
               e.needsApiKey() ? "[需key]" : "");
      }
      fflush(stdout);
      continue;
    }
    if (line.rfind("/use", 0) == 0) {
      std::string arg = line.substr(4);
      const size_t start = arg.find_first_not_of(" \t");
      if (start == std::string::npos) {
        printf("用法: /use <编号> 或 /use <厂商>[/模型] (用 /list 查看)\n");
        continue;
      }
      arg = arg.substr(start);
      const size_t end = arg.find_last_not_of(" \t");
      if (end != std::string::npos) arg = arg.substr(0, end + 1);

      const std::vector<ProviderEntry> entries = listCatalogEntries();
      std::string provider;
      std::string model;
      // 编号选法: 对应 /list 列出顺序。
      try {
        const int index = std::stoi(arg);
        if (index >= 1 && index <= static_cast<int>(entries.size())) {
          provider = entries[index - 1].provider;
          model = entries[index - 1].model;
        }
      } catch (...) {
      }
      // 厂商[/模型] 直接指定。
      if (provider.empty()) {
        const size_t slash = arg.find('/');
        provider = slash == std::string::npos ? arg : arg.substr(0, slash);
        model = slash == std::string::npos ? std::string() : arg.substr(slash + 1);
      }
      if (provider.empty()) {
        printf("厂商名不能为空\n");
        continue;
      }
      // 只给厂商没给模型: 取该厂商第一个可用模型。
      if (model.empty()) {
        for (const ProviderEntry& e : entries) {
          if (e.provider == provider) {
            model = e.model;
            break;
          }
        }
        if (model.empty()) {
          printf("厂商 %s 下没找到模型 (用 /add 新增)\n", provider.c_str());
          continue;
        }
      }
      // 目标必须存在于目录, 否则装配必然失败, 先拦下。
      bool found = false;
      for (const ProviderEntry& e : entries) {
        if (e.provider == provider && e.model == model) {
          found = true;
          break;
        }
      }
      if (!found) {
        printf("目录里没有 %s / %s (用 /list 查看, /add 新增)\n",
               provider.c_str(), model.c_str());
        continue;
      }
      applyDeploymentSelection(runtime, &observer, provider, model);
      continue;
    }
    if (line == "/add") {
      interactiveAddProvider();
      continue;
    }
    if (line == "/raw") {
      gUseRawInput = !gUseRawInput;
      printf("(输入模式: %s)\n",
             gUseRawInput ? "raw — 自管行编辑" : "cooked — 系统行编辑");
      continue;
    }
    if (line == "/clear") {
      if (runtime.composed.host == nullptr) {
        printf("agent 未就绪, 用 /use 先切换配置\n");
        continue;
      }
      // 开一个新会话: 旧会话的日志已经落盘, 不需要"清空"什么 —— 历史是 append-only 的。
      runtime.host()->closeAgent();
      gShell.agent.store(nullptr);
      runtime.agent = runtime.host()->openAgent("");
      if (runtime.agent != nullptr) {
        runtime.agent->session().addObserver(&observer);
        gShell.agent.store(runtime.agent);
      }
      cmdClearScreen();
      printf("(已开新会话: %s)\n", runtime.host()->sessionPath().c_str());
      continue;
    }
    if (line == "/compact") {
      if (runtime.agent == nullptr) continue;
      // 压缩策略挂在 pre-step 上, 这里占一次维护相位让它有机会跑。
      const bool accepted =
          runtime.agent->runMaintenance([](const std::shared_ptr<AbortSignal>&) {});
      printf(accepted ? "(已请求压缩, 将在下一步生效)\n" : "(agent 正忙, 稍后再试)\n");
      continue;
    }
    if (line.rfind("/events", 0) == 0) {
      size_t count = 20;
      const size_t space = line.find(' ');
      if (space != std::string::npos) {
        try {
          count = static_cast<size_t>(std::stoul(line.substr(space + 1)));
        } catch (...) {
        }
      }
      printEvents(runtime, count);
      continue;
    }
    if (line.rfind("/resume", 0) == 0) {
      if (runtime.composed.host == nullptr) {
        printf("agent 未就绪, 用 /use 先切换配置\n");
        continue;
      }
      std::string arg = line.substr(7);
      const size_t start = arg.find_first_not_of(" \t");
      std::string sessionId;
      if (start != std::string::npos) {
        sessionId = arg.substr(start);
        const size_t end = sessionId.find_last_not_of(" \t");
        if (end != std::string::npos) sessionId = sessionId.substr(0, end + 1);
      }

      const std::string sessionRoot =
          runtime.deployment().agent.sessionRoot;
      if (sessionId.empty()) {
        // dsh 布局: <root>/<projectKey(cwd)>/<encodeSegment(id)>/session.jsonl。
        // 只列当前项目 (会话 cwd, 与开会话用同一份配置) 的会话 —— 列表里的 id 必须能
        // 在当前 cwd 下 resume (布局含 cwd)。缺省无 cwd -> 列 _no-cwd。
        const std::string projectDir =
            dshProjectDir(sessionRoot, runtime.deployment().agent.sessionCwd);
        printf("可恢复的会话 (%s):\n", projectDir.c_str());
        struct FoundSession {
          std::string id;
          int64_t createdAt = 0;
        };
        std::vector<FoundSession> found;
        std::error_code ec;
        for (const auto& entry :
             std::filesystem::directory_iterator(projectDir, ec)) {
          if (ec) break;
          if (!entry.is_directory()) continue;
          const std::filesystem::path logFile = entry.path() / "session.jsonl";
          std::error_code readEc;
          if (!std::filesystem::exists(logFile, readEc) || readEc) continue;
          // 只读头行拿 id/createdAt (dsh 的 list 同款: 开销随会话数, 不随日志总大小)。
          std::ifstream in(logFile, std::ios::binary);
          std::string firstLine;
          if (!std::getline(in, firstLine)) continue;
          if (!firstLine.empty() && firstLine.back() == '\r') firstLine.pop_back();
          try {
            const SessionHeader header = decodeHeader(firstLine);
            found.push_back(FoundSession{header.id.value, header.createdAt});
          } catch (...) {
            // 头行解析不了 (版本不符/损坏) 的日志 resume 不了, 跳过但计数提示。
            continue;
          }
        }
        if (found.empty()) {
          printf("  (无)\n");
          continue;
        }
        // 段名是转义过的 id, 目录序不再有时间序 —— 按创建时刻排。
        std::sort(found.begin(), found.end(),
                  [](const FoundSession& a, const FoundSession& b) {
                    return a.createdAt < b.createdAt;
                  });
        for (size_t i = 0; i < found.size(); ++i) {
          printf("  %d. %s\n", static_cast<int>(i + 1), found[i].id.c_str());
        }
        printf("输入编号或会话 id (回车取消): ");
        fflush(stdout);
        std::string selection;
        if (!shellNextLine(selection)) break;
        while (!selection.empty() && (selection.back() == '\r'
                                      || selection.back() == '\n'
                                      || selection.back() == ' ')) {
          selection.pop_back();
        }
        if (selection.empty()) continue;
        try {
          const int index = std::stoi(selection);
          if (index >= 1 && index <= static_cast<int>(found.size())) {
            sessionId = found[index - 1].id;
          }
        } catch (...) {
          sessionId = selection;
        }
        if (sessionId.empty()) sessionId = selection;
      }
      // 去掉可能带上的 .jsonl 后缀。
      if (sessionId.size() > 6 && sessionId.rfind(".jsonl") == sessionId.size() - 6) {
        sessionId = sessionId.substr(0, sessionId.size() - 6);
      }

      runtime.host()->closeAgent();
      gShell.agent.store(nullptr);
      try {
        runtime.agent = runtime.host()->openAgent(sessionId);
      } catch (const std::exception& e) {
        printf("恢复失败: %s\n", e.what());
        runtime.agent = runtime.host()->openAgent("");
        if (runtime.agent != nullptr) {
          runtime.agent->session().addObserver(&observer);
          gShell.agent.store(runtime.agent);
        }
        continue;
      }
      if (runtime.agent != nullptr) {
        runtime.agent->session().addObserver(&observer);
        gShell.agent.store(runtime.agent);
        // resume 恢复的是**完整**历史: 工具调用与结果都在, 这是旧 track 恢复做不到的。
        printf("已恢复会话 %s: %zu 条事件, %zu 条模型历史\n", sessionId.c_str(),
               runtime.agent->session().events().size(),
               runtime.agent->session().deriveMessages().size());
      }
      continue;
    }
    if (line[0] == '/') {
      printf("未知命令: %s (输入 /help 查看)\n", line.c_str());
      continue;
    }

    // ---- 对话 ----
    //
    // 一句话就够: 投递输入 + 等静止。turn/step 的推进、工具执行、历史落盘、压缩、
    // 循环卫生全在驱动与策略里, shell 只负责渲染。
    // 修复模式下 agent 未就绪, 不进入对话, 引导先用命令把配置修好。
    if (runtime.agent == nullptr) {
      printf("(agent 未就绪: 先用 /list 查看、/add 新增或用 /use 切换, 再开始对话)\n");
      continue;
    }
    gCmdHistory.add(line);
    gShell.interruptGen.store(false);
    gShell.generating.store(true);
    // 不进 quiet-input: QuickEdit 保持可用, 回复中随时可框选复制/暂停滚动。框选会冻结
    // conhost 的输出写, 但回复期输出全部经 cmdWriter 写线程落屏 (队列缓冲), 冻住的只是
    // 显示, agent 逻辑照常推进, 松开选区即自动追平。
    observer.beginTurn();

    UserMessage message;
    message.id = MessageId(runtime.agent->id().value + "/user/"
                           + std::to_string(runtime.agent->session().seq()));
    message.content.push_back(TextBlock{line});
    message.source.kind = MessageSourceKind::User;

    cmdWriteF("%s[A]%s ", cmdColor::kGreen, cmdColor::kReset);
    runtime.agent->followup(std::move(message));
    // 无限等: Ctrl+C 会 cancel, driver 随之收敛并让本调用返回。
    runtime.agent->whenIdle(-1);

    observer.endTurn();
    gShell.generating.store(false);

    if (gShell.interruptGen.load()) {
      cmdWriteF("(已中断)\n");
    } else if (!observer.failure().empty()) {
      cmdWriteF("%s(ERROR)%s %s\n", cmdColor::kRed, cmdColor::kReset,
                observer.failure().c_str());
    }
    // 状态栏显示在本轮输入行的下方 (Claude Code 的输入框下方状态条同款)。
    printCreditBar(runtime.agent, runtime.deployment().agent,
                   observer.lastTurn());
    // 屏障: 收尾内容全部落屏后, 再回到输入等待 (下一个 agent> 由输入线程直写)
    cmdWriteFlush();
  }

  // 历史不必显式保存: CmdHistory::add 已经落盘。
  //
  // reset() 是收敛点: 停 driver 并 join、等工具跑完、摘观察者、flush 会话日志。
  runtime.reset();
  // 还原日志 observer: silentOb 是栈对象, 进程退出前显式置空避免 dangling 指针挂在
  // 全局 gLogOb 上 (静态析构顺序不确定, 留下悬空会致后续日志访问违规)。
  setLogObserver(nullptr);
  cmdWriterStop();
  return 0;
}

}
