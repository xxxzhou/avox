#include "BuiltinTools.hpp"

#include <iterator>
#include <utility>

#include "EditTool.hpp"
#include "GlobTool.hpp"
#include "GrepTool.hpp"
#include "PwshTool.hpp"
#include "ReadImageTool.hpp"
#include "ReadTool.hpp"
#include "RunCodeTool.hpp"
#include "SkillTool.hpp"
#include "SubagentTool.hpp"
#include "TeamTools.hpp"
#include "TodoWriteTool.hpp"
#include "WriteTool.hpp"
#include "avox/module/LogHelper.hpp"

namespace avox {

std::vector<Disposer> registerBuiltinTools(AgentHost& host,
                                           AttachmentStore* attachments,
                                           bool conversationImageInput) {
  std::vector<Disposer> disposers;

  const auto define = [&](ToolDefinition definition) {
    const std::string name = definition.name;
    try {
      disposers.push_back(host.defineTool(std::move(definition)));
    } catch (const std::exception& e) {
      // 单个工具注册失败不该拖垮整次装配 (例如某条 cmd 与某条 skill 同名), 但必须留下线索:
      // 那个工具从此对模型不可见, 而模型不会知道它为什么不见了。
      LOGFLF(LogLevel::warn, "[tools] 注册工具 ", name.c_str(), " 失败: ", e.what());
    }
  };

  // 通用工具。
  define(makeReadTool());
  define(makeGrepTool());
  define(makeRunCodeTool());

  // dsh 同名基础工具面 (参数/返回形状/错误文案逐字段对齐 dsh 源码):
  //   write/edit/glob/pwsh/todo_write/read_image。
  define(makeWriteTool());
  define(makeEditTool());
  define(makeGlobTool());
  define(makePwshTool());
  define(makeTodoWriteTool());
  // read_image 只在会话主模型支持图片输入时注册: ImageBlock 会被发给主模型的文本端点,
  // 纯文本主模型 (DeepSeek/glm-4-flash/glm-4.7-flash) 收到 image_url 直接 HTTP 400。
  if (conversationImageInput) {
    define(makeReadImageTool(attachments));
    LOGFLF(LogLevel::info,
           "[tools] 主模型支持图片输入 (imageInput=true), 注册 read_image 作为默认看图工具; "
           "OCR/对比/定位/像素差/抠图/生图等专项任务仍走 vision-toolkit (see-image 系列)");
  } else {
    LOGFLF(LogLevel::info,
           "[tools] 主模型不支持图片输入 (imageInput=false), 不注册 read_image; "
           "看图请用 see-image (vision-toolkit)");
  }

  // 单一 skill 工具 (对齐 dsh): 模型先看 system prompt 的 skill 目录, 选中后按 name
  // 加载该 skill 的完整指令正文。目录里有多少条 skill 不影响工具集 —— 只有这一个加载器。
  define(makeSkillTool());

  // 子代理委派 (对齐 dsh tool-subagent 的前台 one-shot 路径): 模型把自包含子任务派给
  // 瞬态子会话, 结果回灌本会话。按 agent.json 的 subagent.enabled 开关。
  if (host.config().enableSubagent) {
    define(makeSubagentTool(host));
  }

  // Agent Teams durable runtime (对齐 dsh experimental/agent-team + tool-agent-team):
  // 十个 team 工具 + team:policy 提示段。全局层一次注册 —— TeamService 在 openAgent
  // 后才创建, 处理体经 host.team() 晚绑定, 队友与 one-shot 子代理共享本注册表
  // (非成员的调用在归属解析处响亮失败)。按 agent.json 的 team.enabled 开关。
  if (host.config().enableTeam) {
    try {
      std::vector<Disposer> teamDisposers = installTeamTools(host);
      disposers.insert(disposers.end(),
                       std::make_move_iterator(teamDisposers.begin()),
                       std::make_move_iterator(teamDisposers.end()));
    } catch (const std::exception& e) {
      LOGFLF(LogLevel::warn, "[tools] 注册 team 工具面失败: ", e.what());
    }
  }
  return disposers;
}

}
