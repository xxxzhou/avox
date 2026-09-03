#include "SpillPolicy.hpp"

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>

#include "PolicySupport.hpp"
#include "avox/module/LogHelper.hpp"

namespace avox {

namespace {

// 把偏移调整到 UTF-8 字符边界上 (向前退到非 continuation byte)。
//
// 不做这一步的话, 头尾预览会在多字节字符中间切断, 产出一个模型看到的乱码字节 ——
// 而中文日志里这几乎必然发生。
size_t alignBackward(const std::string& text, size_t offset) {
  while (offset > 0 && (static_cast<unsigned char>(text[offset]) & 0xC0) == 0x80) {
    --offset;
  }
  return offset;
}

size_t alignForward(const std::string& text, size_t offset) {
  while (offset < text.size()
         && (static_cast<unsigned char>(text[offset]) & 0xC0) == 0x80) {
    ++offset;
  }
  return offset;
}

// 头尾预览: 预算对半分给两端。
std::string headTailPreview(const std::string& text, size_t budget,
                           size_t& omittedBytes) {
  if (budget == 0) {
    omittedBytes = text.size();
    return std::string();
  }
  if (text.size() <= budget) {
    omittedBytes = 0;
    return text;
  }
  const size_t headBudget = (budget + 1) / 2;
  const size_t tailBudget = budget - headBudget;
  const size_t headEnd = alignBackward(text, headBudget);
  const size_t tailStart = alignForward(text, text.size() - tailBudget);
  omittedBytes = tailStart - headEnd;
  return text.substr(0, headEnd) + "\n…\n" + text.substr(tailStart);
}

std::string spillNotice(size_t omittedBytes, const std::string& path) {
  return "(已省略 " + std::to_string(omittedBytes) + " 字节。完整结果存于: " + path
         + " —— 需要时用 read 读取该文件。)";
}

}  // namespace

Disposer installSpillPolicy(ToolRuntime& tools, SpillPolicyConfig config) {
  // 未配置 = 真正的空操作, 什么都不注册。
  if (config.maxInlineBytes <= 0) return []() {};
  if (config.spillRoot.empty()) {
    throw std::runtime_error("配置了 maxInlineBytes 就必须配置 spillRoot");
  }

  const size_t cap = static_cast<size_t>(config.maxInlineBytes);
  auto counter = std::make_shared<std::atomic<size_t>>(0);
  auto configShared = std::make_shared<SpillPolicyConfig>(std::move(config));

  return tools.postExecute.on(
      [cap, counter, configShared](
          PostToolPayload& payload,
          const Chain<PostToolPayload, PostToolDecision>::Next& next)
          -> PostToolDecision {
        // 先委派: 下游 (hook、工具自己的后处理) 定稿之后我们再裁剪它的结果。
        PostToolDecision decision = next();
        auto* accept = std::get_if<PostToolAccept>(&decision);
        // block 直接放过 —— 溢出策略只塑造被接受的结果, 从不动纠正性反馈。
        if (accept == nullptr) return decision;

        const ToolExecution& exec = *payload.exec;
        const std::vector<std::string>& skip = configShared->skipTools;
        if (std::find(skip.begin(), skip.end(), exec.name) != skip.end()) {
          return decision;
        }

        const std::vector<ContentBlock>& content =
            accept->content.has_value() ? *accept->content : payload.result->content;
        std::string text;
        // 含任何非文本块就不动: 本策略只知道最终格式化文本, 不知道工具内部结构。
        if (!flattenPlainText(content, text)) return decision;
        if (text.size() <= cap) return decision;

        // 落盘。任何一步失败都保留原文 —— 一次存储失败绝不能把成功的调用变成失败。
        std::string spillPath;
        try {
          std::filesystem::create_directories(configShared->spillRoot);
          const size_t serial = ++*counter;
          spillPath = (std::filesystem::path(configShared->spillRoot)
                       / (exec.name + "." + std::to_string(serial) + ".txt"))
                          .string();
          std::ofstream out(spillPath, std::ios::binary | std::ios::trunc);
          if (!out.is_open()) throw std::runtime_error("无法创建溢出文件");
          out.write(text.data(), static_cast<std::streamsize>(text.size()));
          out.flush();
          if (!out.good()) throw std::runtime_error("写入溢出文件失败");
        } catch (const std::exception& e) {
          LOGFLF(LogLevel::warn, "[spill] 保存 ", exec.name.c_str(),
                 " 的溢出结果失败, 保留内联内容: ", e.what());
          return decision;
        }

        // notice 的字节成本先从 cap 里预扣, 且按**最坏情况**的省略字节数定价 (全文长度的
        // 位数不少于真实省略数的位数), 于是预留一定够, 最终替换体永不超 cap。
        const size_t reserve = spillNotice(text.size(), spillPath).size() + 2;
        if (reserve >= cap) {
          // notice 本身就超 cap (cap 太小或路径太长): 没有合法的替换体, 保留原文 ——
          // 硬塞一个超 cap 的替换等于破坏本策略对外承诺的上限。
          LOGFLF(LogLevel::warn, "[spill] ", exec.name.c_str(),
                 " 的溢出提示超过 maxInlineBytes, 保留内联内容");
          return decision;
        }

        size_t omitted = 0;
        const std::string preview = headTailPreview(text, cap - reserve, omitted);
        std::string replaced = preview.empty()
                                   ? spillNotice(omitted, spillPath)
                                   : preview + "\n\n"
                                         + spillNotice(omitted, spillPath);

        std::vector<ContentBlock> blocks;
        blocks.push_back(TextBlock{std::move(replaced)});
        accept->content = std::move(blocks);
        return decision;
      },
      nullptr, /*prepend=*/true);
}

}
