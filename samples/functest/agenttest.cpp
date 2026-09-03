// avox_agent 新架构的端到端测试。
//
// 走 C 导出层 (createAgentHost / IAgentSession / ISessionObserver), 与外部语言绑定走的是
// 同一条路 —— 于是它同时验证了导出层。也因此不必把 core 的源文件编进来: 接口全是裸指针 +
// const char*, 没有 std 容器跨 DLL 边界。
//
// 会真实调用 agent.json 里 "now" 指向的那个模型。用法:
//   agenttest.exe              发一句默认的短问题
//   agenttest.exe "你的问题"    发指定问题

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>

#include "avox_agent/AgentExport.h"

using namespace avox;

namespace {

class TestObserver : public ISessionObserver {
 public:
  void onToken(const char* text) override {
    if (text == nullptr) return;
    std::fputs(text, stdout);
    std::fflush(stdout);
    sawToken = true;
  }

  void onReasoning(const char* text) override {
    if (text == nullptr) return;
    // 推理流单独标记, 免得与正文混淆。
    std::printf("\033[90m%s\033[0m", text);
    std::fflush(stdout);
  }

  void onToolCall(const char* toolName, const char* argsJson) override {
    std::printf("\n[tool] %s %s\n", toolName ? toolName : "?",
                argsJson ? argsJson : "");
    std::fflush(stdout);
    ++toolCalls;
  }

  void onToolResult(const char* toolName, const char* resultText, bool ok) override {
    std::string text = resultText ? resultText : "";
    if (text.size() > 200) text = text.substr(0, 200) + "…";
    std::printf("  -> [%s] %s %s\n", ok ? "ok" : "FAIL", toolName ? toolName : "?",
                text.c_str());
    std::fflush(stdout);
  }

  void onTurnEnd(const char* content, const char* error) override {
    std::printf("\n--- turn 结束 ---\n");
    if (error != nullptr && error[0] != '\0') {
      std::printf("错误: %s\n", error);
      turnError = error;
    }
    if (content != nullptr && content[0] != '\0') {
      finalContent = content;
    }
    std::fflush(stdout);
  }

  void onStatus(int status) override {
    std::printf("[status] %s\n", status == 0 ? "idle" : "running");
    std::fflush(stdout);
  }

  bool sawToken = false;
  int toolCalls = 0;
  std::string finalContent;
  std::string turnError;
};

std::string readFileText(const char* path) {
  std::ifstream file(path, std::ios::binary);
  if (!file.is_open()) return std::string();
  std::ostringstream buffer;
  buffer << file.rdbuf();
  return buffer.str();
}

}  // namespace

int main(int argc, char** argv) {
  // --resume <sessionId>: 只打开既有会话并回放日志结构, 不发问题。
  // 用于回归检查解码 (事件能否原样读回, 而不是崩或降级成 opaque)。
  if (argc == 3 && std::strcmp(argv[1], "--resume") == 0) {
    const std::string config = readFileText("assets/config/agent.json");
    if (config.empty()) {
      std::printf("读不到 assets/config/agent.json (需在 install 目录下运行)\n");
      return 1;
    }
    IAgentHost* host = createAgentHost(config.c_str());
    if (host == nullptr) {
      std::printf("createAgentHost 失败: %s\n", lastAgentHostError());
      return 1;
    }
    IAgentSession* session = host->openAgent(argv[2]);
    if (session == nullptr) {
      std::printf("openAgent(%s) 失败: %s\n", argv[2], host->lastError());
      host->shutdown();
      delete host;
      return 1;
    }
    const size_t total = session->eventCount();
    std::printf("会话 %s 打开成功, 共 %zu 条事件:\n", argv[2], total);
    for (size_t seq = 0; seq < total; ++seq) {
      const char* json = session->eventJson(seq);
      if (json == nullptr) continue;
      std::string line(json);
      if (line.size() > 200) line = line.substr(0, 200) + "…";
      std::printf("  #%zu %s\n", seq, line.c_str());
    }
    host->shutdown();
    delete host;
    std::printf("回放读取通过\n");
    return 0;
  }

  // --cancel-after <ms> [问题]: 发出问题后 ms 毫秒触发一次用户取消。用于回归
  // 「取消流部分前缀收尾」(dsh #2134): 已送达前缀应收尾成 interrupted 的
  // assistant/message, 随后才是 aborted 的 turn/end。
  int cancelAfterMs = -1;
  const char* questionArg = nullptr;
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--cancel-after") == 0 && i + 1 < argc) {
      cancelAfterMs = std::atoi(argv[++i]);
    } else if (questionArg == nullptr) {
      questionArg = argv[i];
    }
  }
  const std::string question = questionArg != nullptr
      ? questionArg
      : "Count from 1 to 300, one number per line with a fact each. Do not stop early.";

  // 配置就是现有的 agent.json (多配置 + now)。
  const std::string config = readFileText("assets/config/agent.json");
  if (config.empty()) {
    std::printf("读不到 assets/config/agent.json (需在 install 目录下运行)\n");
    return 1;
  }

  IAgentHost* host = createAgentHost(config.c_str());
  if (host == nullptr) {
    std::printf("createAgentHost 失败: %s\n", lastAgentHostError());
    return 1;
  }

  IAgentSession* session = host->openAgent("");
  if (session == nullptr) {
    std::printf("openAgent 失败: %s\n", host->lastError());
    host->shutdown();
    delete host;
    return 1;
  }
  std::printf("会话日志: %s\n", session->trackPath());

  TestObserver observer;
  session->addObserver(&observer);

  std::printf("\n>>> %s\n\n", question.c_str());
  if (cancelAfterMs >= 0) {
    std::printf("[test] %d ms 后触发用户取消\n", cancelAfterMs);
    std::thread([session, cancelAfterMs]() {
      std::this_thread::sleep_for(std::chrono::milliseconds(cancelAfterMs));
      session->cancel(0);
    }).detach();
  }
  session->followup(question.c_str());

  // 有限超时: 一次真实往返加上可能的工具执行, 3 分钟足够; 超时则说明卡住了。
  const bool settled = session->waitIdle(180000);
  std::printf("\n===============================\n");
  std::printf("已静止      : %s\n", settled ? "是" : "否 (超时)");
  std::printf("收到 token  : %s\n", observer.sawToken ? "是" : "否");
  std::printf("工具调用    : %d 次\n", observer.toolCalls);
  std::printf("事件数      : %zu\n", session->eventCount());
  if (!observer.finalContent.empty()) {
    std::printf("最终回复    : %s\n", observer.finalContent.c_str());
  }
  if (!observer.turnError.empty()) {
    std::printf("轮次错误    : %s\n", observer.turnError.c_str());
  }

  // 打印前若干条事件类型: 确认边界结构完整 (turn/start → step/start → … → turn/end)。
  std::printf("\n事件序列 (前 12 条):\n");
  const size_t total = session->eventCount();
  for (size_t seq = 0; seq < total && seq < 12; ++seq) {
    const char* json = session->eventJson(seq);
    if (json == nullptr) continue;
    std::string line(json);
    if (line.size() > 160) line = line.substr(0, 160) + "…";
    std::printf("  #%zu %s\n", seq, line.c_str());
  }

  session->removeObserver(&observer);
  // shutdown 是收敛点: 返回后无 driver 线程、无在跑的工具、日志已 flush。
  host->shutdown();
  delete host;

  const bool ok = settled && observer.turnError.empty() && observer.sawToken;
  std::printf("\n%s\n", ok ? "端到端链路通过" : "端到端链路未通过 (见上方错误)");
  return ok ? 0 : 1;
}
