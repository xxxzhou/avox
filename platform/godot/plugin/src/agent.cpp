// agent.cpp — LLM Agent 封装 (avox IAgentHost → Godot 信号)
//
// 线程模型:
//   chat() 异步; 观察者回调在 agent 的 driver 线程触发 → call_deferred("emit_signal", ...)
//   投递 Godot 主线程 (跨线程安全)。
//
//   析构顺序至关重要: shutdown() 必须先返回 (它是收敛点: 停 driver 并 join、等工具跑完、
//   flush 日志), 之后观察者对象才能安全释放。反过来会让 driver 线程回调进一个已析构的 ob。

#include "agent.h"

#include <godot_cpp/core/class_db.hpp>

// Android 构建默认 AVOX_ENABLE_AGENT=OFF (libavox.so 无 createAgentHost 导出),
// 整文件条件编译 (godot_init 的 AgentNode 注册同 guard), 避免链接期未解析符号
#if defined(AVOX_ENABLE_AGENT)

namespace godot {

// ============================================================
// AgentNode::AgentOb — 会话事件 → 主线程信号
// ============================================================

class AgentNode::AgentOb : public avox::ISessionObserver {
public:
    AgentNode *owner = nullptr;
    // 当前轮次 id: chat() 递增后交给本 ob, 信号带它出去。
    int requestId = 0;

    void onToken(const char *text) override {
        if (owner && text)
            owner->call_deferred("emit_signal", "agent_token", requestId, String::utf8(text));
    }

    void onReasoning(const char *text) override {
        if (owner && text)
            owner->call_deferred("emit_signal", "agent_reasoning", requestId, String::utf8(text));
    }

    void onToolCall(const char *toolName, const char *argsJson) override {
        if (!owner) return;
        owner->call_deferred("emit_signal", "tool_call", requestId,
                             toolName ? String::utf8(toolName) : String(),
                             argsJson ? String::utf8(argsJson) : String());
    }

    void onToolResult(const char *toolName, const char *resultText, bool ok) override {
        if (!owner) return;
        owner->call_deferred("emit_signal", "tool_result", requestId,
                             toolName ? String::utf8(toolName) : String(),
                             resultText ? String::utf8(resultText) : String(), ok);
    }

    void onTurnEnd(const char *content, const char *error) override {
        if (!owner) return;
        owner->call_deferred("emit_signal", "agent_result", requestId,
                             content ? String::utf8(content) : String(),
                             error ? String::utf8(error) : String());
    }
};

// ============================================================
// AgentNode
// ============================================================

AgentNode::AgentNode() { agentOb = std::make_unique<AgentOb>(); }

AgentNode::~AgentNode() {
    if (host != nullptr) {
        // shutdown 是收敛点: 返回后无 driver 线程、无在跑的工具, 观察者可安全释放。
        host->shutdown();
        delete host;
        host = nullptr;
        session = nullptr;
    }
    // agentOb (unique_ptr) 随后自动析构。
}

bool AgentNode::configure(const String &p_url, const String &p_api_key, const String &p_model) {
    // 重新配置 = 重建宿主: 路由变了, 策略与提示词都要跟着重装。
    if (host != nullptr) {
        host->shutdown();
        delete host;
        host = nullptr;
        session = nullptr;
    }

    // 单配置的 agent.json 形态: 一个具名配置 + now 指向它。
    String config = String("{\"godot\":{\"provider\":\"openai\",\"url\":\"")
                    + p_url.json_escape() + String("\",\"apiPath\":\"/chat/completions\",\"apiKey\":\"")
                    + p_api_key.json_escape() + String("\",\"model\":\"")
                    + p_model.json_escape() + String("\"},\"now\":\"godot\"}");

    host = avox::createAgentHost(config.utf8().get_data());
    if (host == nullptr) {
        const char *reason = avox::lastAgentHostError();
        lastError = reason ? String::utf8(reason) : String("createAgentHost 失败");
        return false;
    }
    if (!systemPrompt.is_empty()) {
        // order 0 = 部署人格的约定位置。
        host->addPromptSection("godot:persona", 0, systemPrompt.utf8().get_data());
    }

    session = host->openAgent("");
    if (session == nullptr) {
        const char *reason = host->lastError();
        lastError = reason ? String::utf8(reason) : String("openAgent 失败");
        return false;
    }
    agentOb->owner = this;
    session->addObserver(agentOb.get());
    lastError = String();
    return true;
}

void AgentNode::setSystemPrompt(const String &p_sys) {
    systemPrompt = p_sys;
    // 已有会话时立即生效 (代价: KV cache 前缀从此处失效)。
    if (host != nullptr && !systemPrompt.is_empty()) {
        host->addPromptSection("godot:persona", 0, systemPrompt.utf8().get_data());
    }
}

String AgentNode::getSystemPrompt() const { return systemPrompt; }

int AgentNode::chat(const String &p_text) {
    if (session == nullptr) return 0;
    const int requestId = ++nextRequestId;
    agentOb->requestId = requestId;
    session->followup(p_text.utf8().get_data());
    return requestId;
}

void AgentNode::cancel() {
    // 0 = user 取消。
    if (session != nullptr) session->cancel(0);
}

void AgentNode::addUserMessage(const String &p_text) {
    // inject: 进下一次 pre-step 的上下文但不唤醒驱动 —— 对应旧的"只入历史不发送"。
    if (session != nullptr) session->inject(p_text.utf8().get_data());
}

void AgentNode::clearMessages() {
    if (host == nullptr) return;
    if (session != nullptr) session->removeObserver(agentOb.get());
    host->closeAgent();
    session = host->openAgent("");
    if (session != nullptr) session->addObserver(agentOb.get());
}

bool AgentNode::available() const { return session != nullptr; }

String AgentNode::getLastError() const {
    if (host == nullptr) return lastError;
    const char *reason = host->lastError();
    if (reason != nullptr && reason[0] != '\0') return String::utf8(reason);
    return lastError;
}

void AgentNode::_notification(int p_what) {
    // EXIT_TREE 仅取消在途轮次 (析构负责 shutdown): 让 driver 尽早停, 避免退出期回调。
    if (p_what == NOTIFICATION_EXIT_TREE) cancel();
}

void AgentNode::_bind_methods() {
    ClassDB::bind_method(D_METHOD("configure", "url", "api_key", "model"), &AgentNode::configure);
    ClassDB::bind_method(D_METHOD("set_system_prompt", "sys"), &AgentNode::setSystemPrompt);
    ClassDB::bind_method(D_METHOD("get_system_prompt"), &AgentNode::getSystemPrompt);
    ClassDB::bind_method(D_METHOD("chat", "text"), &AgentNode::chat);
    ClassDB::bind_method(D_METHOD("cancel"), &AgentNode::cancel);
    ClassDB::bind_method(D_METHOD("add_user_message", "text"), &AgentNode::addUserMessage);
    ClassDB::bind_method(D_METHOD("clear_messages"), &AgentNode::clearMessages);
    ClassDB::bind_method(D_METHOD("is_available"), &AgentNode::available);
    ClassDB::bind_method(D_METHOD("get_last_error"), &AgentNode::getLastError);
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "system_prompt", PROPERTY_HINT_MULTILINE_TEXT),
                 "set_system_prompt", "get_system_prompt");
    // 流式 token: 实时 TTS / 字幕输入源 (每 token 一帧, driver 线程 → 主线程)
    ADD_SIGNAL(MethodInfo("agent_token",
                          PropertyInfo(Variant::INT, "request_id"),
                          PropertyInfo(Variant::STRING, "token")));
    ADD_SIGNAL(MethodInfo("agent_reasoning",
                          PropertyInfo(Variant::INT, "request_id"),
                          PropertyInfo(Variant::STRING, "token")));
    // 虚拟人控制指令通道: LLM tool_call 经此到 GDScript (set_emotion / play_gesture)
    ADD_SIGNAL(MethodInfo("tool_call",
                          PropertyInfo(Variant::INT, "request_id"),
                          PropertyInfo(Variant::STRING, "name"),
                          PropertyInfo(Variant::STRING, "args_json")));
    ADD_SIGNAL(MethodInfo("tool_result",
                          PropertyInfo(Variant::INT, "request_id"),
                          PropertyInfo(Variant::STRING, "name"),
                          PropertyInfo(Variant::STRING, "result_json"),
                          PropertyInfo(Variant::BOOL, "ok")));
    ADD_SIGNAL(MethodInfo("agent_result",
                          PropertyInfo(Variant::INT, "request_id"),
                          PropertyInfo(Variant::STRING, "content"),
                          PropertyInfo(Variant::STRING, "error")));
}

} // namespace godot

#endif // AVOX_ENABLE_AGENT
