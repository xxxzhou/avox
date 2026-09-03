// chat_render.js — 所有 .msg.* DOM 渲染集中在这里
// 从 agent_test.js 拆出: tool-result 折叠、todo 卡、markdown 渲染。
// 用法: const renderers = ChatRender.make({ dom, state, isAtBottom });
//       renderers.appendMessage(...); renderers.renderTodoCard(args); renderers.markdown(text);
//
// 注册新工具的专属渲染 = 往 TOOL_RENDERERS 加一行, 不用改 agent_test.js 控制流。
(function () {
  'use strict';

  // ---- 纯函数 (无依赖) ----
  function escapeHtml(s) {
    return String(s).replace(/[&<>"']/g, ch => ({
      '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;'
    }[ch]));
  }
  function safeUrl(url) {
    const u = String(url).trim();
    // http(s)/mailto/锚点/相对路径允许; javascript:/data:/vbscript: 等拒掉换成 #
    if (/^(https?:\/\/|mailto:|#|\/|\.{1,2}\/)/i.test(u)) return u;
    return '#';
  }
  // markdown 工具结果摘要: 第一行非空 + size hint
  function firstNonEmptyLine(text) {
    if (!text) return '';
    const lines = String(text).split(/\r?\n/);
    for (const line of lines) {
      const t = line.trim();
      if (t) return t;
    }
    return '';
  }
  function buildToolResultSummary(body) {
    if (!body) return { line: '[空结果]', size: 0 };
    const text = String(body);
    let line = firstNonEmptyLine(text);
    if (line.length > 200) line = line.slice(0, 200) + '…';
    return { line, size: text.length };
  }
  // 摘要行专用: 剥掉常见 markdown 标记 (强调/代码/链接), 只留纯文本预览
  function plainPreview(line) {
    return String(line)
      .replace(/`([^`]*)`/g, '$1')
      .replace(/\*\*([^*]+)\*\*/g, '$1')
      .replace(/\*([^*]+)\*/g, '$1')
      .replace(/\[([^\]]*)\]\([^)]*\)/g, '$1');
  }
  function isMarkdownCls(cls) {
    if (!cls) return false;
    if (cls.indexOf('assistant') === 0) return true;
    if (cls.indexOf('reasoning') === 0) return true;
    return false;
  }
  // 内联解析: 先 stash inline code (原文, 不预先 escape, 避免双转义), 再 escape + bold/italic/link, 最后还原 code 时再 escape.
  function renderInline(text) {
    const stash = [];
    let s = String(text).replace(/`([^`\n]+)`/g, (m, code) => {
      stash.push(code);
      return '\x00' + (stash.length - 1) + '\x00';
    });
    s = escapeHtml(s);
    s = s.replace(/\*\*([^*\n]+)\*\*/g, '<strong>$1</strong>');
    s = s.replace(/(^|[^*\w])\*([^*\n]+)\*(?!\*)/g, '$1<em>$2</em>');
    s = s.replace(/\[([^\]\n]+)\]\(([^)\s]+)\)/g, (m, t, u) =>
      '<a href="' + escapeHtml(safeUrl(u)) + '" target="_blank" rel="noopener noreferrer">' + t + '</a>');
    s = s.replace(/\x00(\d+)\x00/g, (m, i) => '<code>' + escapeHtml(stash[+i]) + '</code>');
    return s;
  }
  // 块级 markdown 解析: code block / heading / hr / table / blockquote / list / paragraph
  // 分隔行形如 |---|---|  或 ---|---  或 :---:|---:  (开头/结尾的 | 可省, 但至少有一个 |)
  function looksLikeTableSeparator(line) {
    const s = line.trim();
    if (!s) return false;
    if (!/^[\s:|-]+$/.test(s)) return false; // 只允许 空格 / 冒号 / 短横 / 竖线
    if (!s.includes('|')) return false;       // 至少要有一个 | 分隔
    if (!s.includes('-')) return false;       // 至少要有一个 -
    return true;
  }
  function renderMarkdown(text) {
    if (text == null) return '';
    const lines = String(text).replace(/\r\n?/g, '\n').split('\n');
    const out = [];
    let i = 0;
    while (i < lines.length) {
      const line = lines[i];
      // fenced code block
      if (/^```/.test(line)) {
        const lang = line.replace(/^```/, '').trim();
        const buf = [];
        i++;
        while (i < lines.length && !/^```/.test(lines[i])) {
          buf.push(escapeHtml(lines[i]));
          i++;
        }
        if (i < lines.length) i++;
        out.push('<pre><code' + (lang ? ' class="lang-' + escapeHtml(lang) + '"' : '') + '>'
          + buf.join('\n') + '</code></pre>');
        continue;
      }
      // heading
      const h = line.match(/^(#{1,6})\s+(.+?)\s*#*\s*$/);
      if (h) {
        const lvl = h[1].length;
        out.push('<h' + lvl + '>' + renderInline(h[2]) + '</h' + lvl + '>');
        i++;
        continue;
      }
      // horizontal rule
      if (/^(\s*[-*_])\s*\1\s*\1[\s\S]*$/.test(line) && /^[-*_]/.test(line.trim())) {
        out.push('<hr>');
        i++;
        continue;
      }
      // table: header | sep | rows
      if (/^\s*\|.*\|\s*$/.test(line) && i + 1 < lines.length
          && looksLikeTableSeparator(lines[i + 1])) {
        const splitCells = (s) => s.replace(/^\s*\|/, '').replace(/\|\s*$/, '')
          .split('|').map(c => c.trim());
        const headerCells = splitCells(line);
        const sepCells = splitCells(lines[i + 1]);
        const aligns = sepCells.map(c => {
          if (/^:-+:$/.test(c)) return 'center';
          if (/^-+:$/.test(c)) return 'right';
          if (/^:-+$/.test(c)) return 'left';
          return '';
        });
        i += 2;
        const rows = [];
        while (i < lines.length && /^\s*\|.*\|\s*$/.test(lines[i])) {
          rows.push(splitCells(lines[i]));
          i++;
        }
        const ths = headerCells.map((c, idx) =>
          '<th style="text-align:' + (aligns[idx] || 'left') + '">' + renderInline(c) + '</th>').join('');
        const trs = rows.map(r => '<tr>' + r.map((c, idx) =>
          '<td style="text-align:' + (aligns[idx] || 'left') + '">' + renderInline(c) + '</td>').join('') + '</tr>').join('');
        out.push('<table><thead><tr>' + ths + '</tr></thead><tbody>' + trs + '</tbody></table>');
        continue;
      }
      // blockquote
      if (/^>\s?/.test(line)) {
        const buf = [];
        while (i < lines.length && /^>\s?/.test(lines[i])) {
          buf.push(lines[i].replace(/^>\s?/, ''));
          i++;
        }
        out.push('<blockquote>' + renderInline(buf.join(' ')) + '</blockquote>');
        continue;
      }
      // ordered / unordered list
      const olMatch = line.match(/^\s*(\d+)\.\s+(.*)$/);
      const ulMatch = line.match(/^\s*[-*]\s+(.*)$/);
      if (olMatch || ulMatch) {
        const ordered = !!olMatch;
        const buf = [];
        const pat = ordered ? /^\s*\d+\.\s+(.*)$/ : /^\s*[-*]\s+(.*)$/;
        while (i < lines.length && pat.test(lines[i])) {
          buf.push(lines[i].replace(pat, '$1'));
          i++;
        }
        const tag = ordered ? 'ol' : 'ul';
        out.push('<' + tag + '>' + buf.map(item => '<li>' + renderInline(item) + '</li>').join('') + '</' + tag + '>');
        continue;
      }
      // 空行 = 段落分隔
      if (/^\s*$/.test(line)) { i++; continue; }
      // paragraph: 累积连续非空行直到下一个块级标记
      const buf = [line];
      i++;
      while (i < lines.length && lines[i].trim()
          && !/^(#{1,6}\s|>\s?|```|\s*[-*]\s|\s*\d+\.\s|\s*\|)/.test(lines[i])
          && !/^(\s*[-*_])\s*\1/.test(lines[i])) {
        buf.push(lines[i]);
        i++;
      }
      out.push('<p>' + renderInline(buf.join('\n').replace(/\n/g, '<br>')) + '</p>');
    }
    return out.join('\n');
  }

  // ---- factory: 闭包持有 dom/state, 返回所有 .msg.* 渲染函数 ----
  function make(deps) {
    if (!deps || !deps.dom || !deps.state) {
      throw new Error('ChatRender.make 需要 { dom, state }');
    }
    const { dom, state } = deps;
    const isAtBottom = deps.isAtBottom || function (threshold) {
      const el = dom.messages;
      if (!el) return true;
      const t = threshold == null ? 40 : threshold;
      return el.scrollHeight - el.scrollTop - el.clientHeight < t;
    };

    // 共享折叠卡构造: summary 露 meta + 第一行摘要 + [N 字], 点开才渲染 body
    // tool-call / tool-result 都走这条路径, 默认折叠, 不在底不抢视口
    // asMarkdown 时 body 走 markdown 渲染 (reasoning 用), 否则 textContent 兜底
    function buildSummaryRow(meta, summaryLine, size, placeholder) {
      const summary = document.createElement('summary');
      if (meta) {
        const m = document.createElement('span');
        m.className = 'meta';
        m.textContent = meta;
        summary.appendChild(m);
      }
      const sl = document.createElement('span');
      sl.className = 'summary-line';
      sl.textContent = summaryLine || placeholder || '[空结果]';
      sl.title = sl.textContent;
      summary.appendChild(sl);
      const sizeHint = document.createElement('span');
      sizeHint.className = 'size-hint';
      sizeHint.textContent = '[' + size + ' 字]';
      summary.appendChild(sizeHint);
      return summary;
    }
    function buildCollapsibleCard(meta, summaryLine, size, body, asMarkdown) {
      const details = document.createElement('details');
      details.appendChild(buildSummaryRow(meta, summaryLine, size));
      const b = document.createElement('span');
      b.className = 'body';
      if (asMarkdown) {
        b.innerHTML = renderMarkdown(body == null ? '' : String(body));
      } else {
        b.textContent = body;
      }
      details.appendChild(b);
      return details;
    }

    // 创建一条消息 div 并追加到 messages 容器。返回该元素。
    // - tool-call* / tool-result* 走 details/summary 折叠, summary 露 meta + 第一行 + [N 字]
    // - assistant/reasoning 走 innerHTML = markdown(text)
    // - 其他走 textContent 兜底
    function appendMessage(cls, meta, body) {
      if (dom.messagesEmpty && dom.messagesEmpty.parentNode) {
        dom.messagesEmpty.remove();
      }
      const el = document.createElement('div');
      el.className = 'msg ' + cls;
      if (cls && cls.indexOf('tool-result') === 0 && body != null) {
        const summaryInfo = buildToolResultSummary(body);
        el.appendChild(buildCollapsibleCard(meta, summaryInfo.line, summaryInfo.size, body));
      } else if (cls && cls.indexOf('tool-call') === 0 && body != null) {
        // tool-call 折叠: 默认闭, summary 露 tool 名 + 第一行参数 + size
        // 跟 tool-result 同款结构, 用户点开才看 pretty args JSON
        const summaryInfo = buildToolResultSummary(body);
        el.appendChild(buildCollapsibleCard(meta, summaryInfo.line, summaryInfo.size, body));
      } else if (cls && cls.indexOf('reasoning') === 0) {
        // reasoning (思考过程) 也走折叠卡: 默认只剩一行摘要不抢视口,
        // body 用 markdown 渲染; 流式更新走 updateReasoning
        const summaryInfo = buildToolResultSummary(body || '');
        const placeholder = (summaryInfo.line && summaryInfo.line !== '[空结果]')
          ? plainPreview(summaryInfo.line) : '[思考中…]';
        el.appendChild(buildCollapsibleCard(meta, placeholder, summaryInfo.size, body || '', true));
      } else {
        if (meta) {
          const m = document.createElement('span');
          m.className = 'meta';
          m.textContent = meta;
          el.appendChild(m);
        }
        if (body != null) {
          const b = document.createElement('span');
          b.className = 'body';
          if (isMarkdownCls(cls)) {
            b.innerHTML = renderMarkdown(body);
          } else {
            b.textContent = body;
          }
          el.appendChild(b);
        }
      }
      const stick = isAtBottom();
      dom.messages.appendChild(el);
      if (stick) dom.messages.scrollTop = dom.messages.scrollHeight;
      return el;
    }

    // reasoning 流式刷新: 全文重渲 markdown 到 .body, 同步 summary 首行摘要 + 字数。
    // el 是 appendMessage 返回的外层 .msg.reasoning 元素。
    function updateReasoning(el, fullText) {
      if (!el) return;
      const text = fullText || '';
      const info = buildToolResultSummary(text);
      const line = (info.line && info.line !== '[空结果]') ? plainPreview(info.line) : '[思考中…]';
      const sl = el.querySelector('details > summary .summary-line');
      if (sl) {
        sl.textContent = line;
        sl.title = sl.textContent;
      }
      const sizeHint = el.querySelector('details > summary .size-hint');
      if (sizeHint) sizeHint.textContent = '[' + info.size + ' 字]';
      const b = el.querySelector('.body');
      if (b) b.innerHTML = renderMarkdown(text);
    }

    // todo_write: 解析 args.todos[] 渲染为有序列表卡片。重复调用原地更新, 不重建卡片。
    function renderTodoCard(argsJson) {
      let todos = [];
      try {
        const obj = JSON.parse(argsJson);
        if (obj && Array.isArray(obj.todos)) todos = obj.todos;
      } catch (_) { return; }
      const stick = isAtBottom();
      if (!state.todoEl) {
        state.todoEl = appendMessage('todo', '[todo]', null);
      }
      const old = state.todoEl.querySelector('.body');
      if (old) old.remove();
      const b = document.createElement('ol');
      // 类名必须含 'body': 上面 querySelector('.body') 是 chat_render.js 内部约定
      // (appendMessage / renderRunCodeCard 都靠它定位并替换 body); 漏挂会导致
      // todo 卡内旧 <ol> 累积, meta 计数对, body 越堆越多.
      b.className = 'body todo-list';
      const counts = { pending: 0, in_progress: 0, completed: 0 };
      todos.forEach((t, i) => {
        const li = document.createElement('li');
        li.className = 'todo-item todo-' + (t.status || 'pending');
        const box = document.createElement('span');
        box.className = 'todo-box';
        box.textContent = t.status === 'completed' ? '☑'
          : t.status === 'in_progress' ? '◐'
            : '☐';
        const num = document.createElement('span');
        num.className = 'todo-num';
        num.textContent = (i + 1) + '.';
        const txt = document.createElement('span');
        txt.className = 'todo-text';
        txt.textContent = t.content || '';
        li.appendChild(box);
        li.appendChild(num);
        li.appendChild(txt);
        b.appendChild(li);
        counts[t.status] = (counts[t.status] || 0) + 1;
      });
      state.todoEl.appendChild(b);
      const metaEl = state.todoEl.querySelector('.meta');
      if (metaEl) {
        metaEl.textContent = '[todo] '
          + (counts.pending || 0) + ' pending, '
          + (counts.in_progress || 0) + ' in progress, '
          + (counts.completed || 0) + ' completed';
      }
      if (stick) dom.messages.scrollTop = dom.messages.scrollHeight;
    }

    // run_code: code + description 两字段。通用渲染把整段 JSON pretty-print 会让
    // 代码里的 \n 变字面 \n, 一坨挤一行看不清。专属渲染: 标题带 description, body
    // 走 <pre><code> 保换行 + lang 提示。默认折叠。
    function renderRunCodeCard(argsJson, callId) {
      let args = {};
      try { args = JSON.parse(argsJson) || {}; } catch (_) { return; }
      const description = (args.description || '').toString();
      const code = (args.code || '').toString();
      const language = (args.language || 'text').toString();
      if (!code && !description) return;

      const stick = isAtBottom();
      const el = document.createElement('div');
      el.className = 'msg tool-call';
      const meta = '[call]' + (callId ? ' ' + callId : '') + ' run_code'
        + (description ? ' · ' + description : '');
      const summaryInfo = buildToolResultSummary(code || description);

      const details = document.createElement('details');
      const summary = document.createElement('summary');
      const m = document.createElement('span');
      m.className = 'meta';
      m.textContent = meta;
      summary.appendChild(m);
      const sl = document.createElement('span');
      sl.className = 'summary-line';
      sl.textContent = description || summaryInfo.line || '[空]';
      sl.title = sl.textContent;
      summary.appendChild(sl);
      const sizeHint = document.createElement('span');
      sizeHint.className = 'size-hint';
      sizeHint.textContent = '[' + (code.length || description.length) + ' 字]';
      summary.appendChild(sizeHint);
      details.appendChild(summary);

      const body = document.createElement('div');
      body.className = 'body';
      if (description) {
        const desc = document.createElement('div');
        desc.className = 'rc-desc';
        desc.textContent = description;
        body.appendChild(desc);
      }
      const pre = document.createElement('pre');
      const codeEl = document.createElement('code');
      if (language) codeEl.className = 'lang-' + language;
      codeEl.textContent = code;
      pre.appendChild(codeEl);
      body.appendChild(pre);
      details.appendChild(body);

      el.appendChild(details);
      if (dom.messagesEmpty && dom.messagesEmpty.parentNode) {
        dom.messagesEmpty.remove();
      }
      dom.messages.appendChild(el);
      if (stick) dom.messages.scrollTop = dom.messages.scrollHeight;
    }

    // 工具专属 renderer 注册表: name → (argsJson, callId) → 渲染到 messages。
    // 未命中走通用 tool-call 卡 (agent_test.handleToolCall fallback)。
    const TOOL_RENDERERS = {
      todo_write: (argsJson) => renderTodoCard(argsJson),
      run_code:   (argsJson, callId) => renderRunCodeCard(argsJson, callId),
      // 扩展点:
      // skill:    (argsJson, callId) => renderSkillCard(argsJson, callId),
      // grep:     (argsJson, callId) => renderGrepCard(argsJson, callId),
      // read:     (argsJson, callId) => renderReadCard(argsJson, callId),
      // run_code: (argsJson, callId) => renderRunCodeCard(argsJson, callId),
    };

    return {
      appendMessage,
      renderTodoCard,
      updateReasoning,
      TOOL_RENDERERS,
      markdown: renderMarkdown,
      isMarkdownCls,
    };
  }

  window.ChatRender = {
    make,
    // 暴露纯函数供独立使用 / 测试
    escapeHtml,
    safeUrl,
    renderInline,
    markdown: renderMarkdown,
    isMarkdownCls,
    buildToolResultSummary,
  };
})();