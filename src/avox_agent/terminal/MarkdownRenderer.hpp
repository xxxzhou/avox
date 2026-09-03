#pragma once

#include <string>

#include "avox_cmd/CmdHelper.hpp"   // cmdColor::*

namespace avox {

// ========== 流式 Markdown 渲染器 (轻量, 与 spinner/卡片交错) ==========
// 不是整文档渲染 (那样要等全部 token 到齐, 无法流式); 而是流式状态机:
// 逐 token 喂入, 维护"当前在/不在代码块""列表缩进"等状态, 边收边产出 (带 ANSI 着色)。
// 设计约束:
//   - 不直接写 stdout: 渲染结果 append 到 public 成员 pending, 调用方一次 cmdWrite
//     落屏 (整段原子入队, 避免 conhost 框选冻结/多线程交错把一行撕开), 取走后 clear。
//   - 输出与 ConsoleThrobber 互斥 (调用方加锁), 不自管锁。
class MarkdownRenderer {
 public:
  // 喂入一个 token 片段, 渲染结果累积进 pending。flushToken=true 在回复结束时调,
  // 刷出缓冲里尚未成形的行 (如未闭合的代码块残片)。
  void render(const std::string& token) {
    for (size_t i = 0; i < token.size(); ++i) {
      char c = token[i];
      if (inCode) {
        renderCodeChar(c);
      } else {
        renderNormalChar(c);
      }
    }
  }
  // 回复结束: 若残留行缓冲有内容, 输出之; 若在代码块内未闭合, 关闭着色 (容错)
  void flush() {
    if (!lineBuf.empty()) { flushLine(); }
    if (inCode) { pending += cmdColor::kReset; inCode = false; }
  }

  // 渲染产出累积处: 调用方 (observer) 每次 render/flush 后整体 cmdWrite 并 clear
  std::string pending;

 private:
  void renderNormalChar(char c) {
    if (c == '\n') {
      flushLine();
    } else {
      lineBuf.push_back(c);
    }
  }
  void renderCodeChar(char c) {
    if (c == '\n') {
      // 代码行: 直接输出 (已在代码块着色内)
      pending += lineBuf;
      pending += '\n';
      lineBuf.clear();
    } else {
      lineBuf.push_back(c);
    }
  }
  void flushLine() {
    // 检测代码块围栏 ``` (可带语言标识, 如 ```python)
    if (lineBuf.size() >= 3 && lineBuf.substr(0, 3) == "```") {
      if (!inCode) {
        // 进入代码块: 着色语言标识 (紫色) + 开启 dim/gray 代码色
        inCode = true;
        std::string lang = lineBuf.substr(3);
        // 去空白
        while (!lang.empty() && (lang.back() == ' ' || lang.back() == '\t' || lang.back() == '\r'))
          lang.pop_back();
        pending += cmdColor::kDim;
        pending += "```";
        if (!lang.empty()) {
          pending += cmdColor::kPurple;
          pending += lang;
        }
        pending += cmdColor::kGray;
        pending += '\n';
      } else {
        // 出代码块: 关闭着色
        inCode = false;
        pending += cmdColor::kReset;
        pending += "```\n";
      }
      lineBuf.clear();
      return;
    }
    if (inCode) {
      // 代码块内的普通行 (非围栏): 直接输出
      pending += lineBuf;
      pending += '\n';
    } else {
      // 普通行: 渲染行内格式 (标题/列表/行内code)
      renderInlineLine(lineBuf);
      pending += '\n';
    }
    lineBuf.clear();
  }
  // 行内渲染: 标题 # / 列表 - / 行内 `code` / 粗体 **x**
  void renderInlineLine(const std::string& line) {
    // trim 尾部 \r
    std::string s = line;
    while (!s.empty() && s.back() == '\r') s.pop_back();
    if (s.empty()) { return; }
    // 标题: # ~ ######
    if (s.size() >= 2 && s[0] == '#') {
      size_t n = 0;
      while (n < s.size() && s[n] == '#') ++n;
      if (n <= 6 && (n == s.size() || s[n] == ' ')) {
        pending += cmdColor::kBold;
        pending += cmdColor::kPurple;
        pending += s;
        pending += cmdColor::kReset;
        return;
      }
    }
    // 列表: - / * / 数字. 开头
    if ((s.size() >= 2 && (s[0] == '-' || s[0] == '*') && s[1] == ' ') ||
        (s.size() >= 3 && s[0] >= '0' && s[0] <= '9' && s[1] == '.' && s[2] == ' ')) {
      pending += cmdColor::kCyan;
      // 输出列表标记
      size_t markEnd = (s[0] == '-' || s[0] == '*') ? 2 : 3;
      pending += s.substr(0, markEnd);
      pending += cmdColor::kReset;
      renderInlineCode(s.substr(markEnd));
      return;
    }
    // 普通行: 行内 code / 粗体
    renderInlineCode(s);
  }
  // 行内 `code` 与 **bold**: 扫描转义
  void renderInlineCode(const std::string& s) {
    bool inInline = false;
    bool inBold = false;
    for (size_t i = 0; i < s.size(); ++i) {
      // 行内 code: `...`
      if (s[i] == '`') {
        if (!inInline) { pending += cmdColor::kCyan; inInline = true; }
        else { pending += cmdColor::kReset; inInline = false; }
        pending += '`';
        continue;
      }
      // 粗体: **...**
      if (i + 1 < s.size() && s[i] == '*' && s[i + 1] == '*') {
        if (!inBold) { pending += cmdColor::kBold; inBold = true; }
        else { pending += cmdColor::kReset; inBold = false; }
        ++i;   // 跳过第二个 *
        continue;
      }
      pending += s[i];
    }
    if (inInline || inBold) pending += cmdColor::kReset;
  }

  bool inCode = false;          // 是否在 ``` 代码块内
  std::string lineBuf;          // 当前行缓冲 (未遇 \n)
};

}
