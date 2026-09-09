#pragma once

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/variant/string.hpp>

#include "avox_agent/AgentExport.h"   // IAgentHost / IAgentSession / ISessionObserver

#include <memory>

namespace godot {

// LLM Agent 客户端封装 (avox IAgentHost → Godot 信号)。
//
// chat() 异步, 回调在 agent 的 driver 线程 → call_deferred 投递主线程:
//   agent_token(request_id, token)     流式 token, 实时 TTS / 字幕的输入源
//   tool_call(request_id, name, json)  虚拟人控制指令通道 (set_emotion / play_gesture)
//   agent_result(request_id, content, error)
//
// 与旧 IAgentClient 版的行为差异 (都是新架构带来的):
//   * 每轮对话的完整轨迹 (含工具调用与结果) 落进会话日志, 可 resume;
//   * request_id 由本节点自增维护 —— 新架构里"一轮"是 turn, 不再有请求级 id;
//   * clear_messages 等于开一个新会话 (历史是 append-only 的, 没有"清空"这个操作)。
class AgentNode : public Node {
    GDCLASS(AgentNode, Node)

public:
    AgentNode();
    ~AgentNode();

    // 配置并打开会话 (chat 前调): OpenAI 兼容 url + apiKey + model。
    bool configure(const String &p_url, const String &p_api_key, const String &p_model);
    // 系统提示词 (虚拟人人设 / 行为约束); 空 = 不注入。必须在 configure 之前调 ——
    // 它进 KV cache 前缀, 会话开始后再改会让缓存失效。
    void setSystemPrompt(const String &p_sys);
    String getSystemPrompt() const;

    // 单条文本对话 (排一个新 turn 并唤醒驱动); 返回 request_id, 0 = 失败。
    int chat(const String &p_text);
    // 中断当前在途轮次 (跨线程安全)。
    void cancel();

    // 追加一条模型可见上下文, 不触发生成 (对应新架构的 inject)。
    void addUserMessage(const String &p_text);
    // 开一个新会话 (旧会话日志已落盘)。
    void clearMessages();

    bool available() const;
    String getLastError() const;

protected:
    static void _bind_methods();
    void _notification(int p_what);

private:
    class AgentOb;

    avox::IAgentHost *host = nullptr;
    // 借用指针: 由 host 拥有, 不要 delete。
    avox::IAgentSession *session = nullptr;
    std::unique_ptr<AgentOb> agentOb;
    String systemPrompt;
    String lastError;
    int nextRequestId = 0;
};

} // namespace godot
