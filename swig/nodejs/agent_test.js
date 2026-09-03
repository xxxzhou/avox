// agent_test.js — chat-first UI: 顶部一行模型选择 + 状态条, 中间对话区, 底部输入框;
// host/session 在页面加载后**自动**按所选模型启动; 设置齿轮展开高级 / skill / 日志面板.
//
// 跨 contextBridge 边界, host/session 的方法在 napi ObjectWrap prototype 上, contextBridge
// 不复制 — 全部走 window.avox.* 离散 IPC (见 avox_agent.js 的 AgentHost/AgentSession 包装).
//
// 审批 ask 是 driver 线程同步阻塞 — contextBridge 跨进程异步, 不能真同步. 用 observer
// onToolCall 提前打开 dialog → 用户点按 → stageApprovalAnswer 预置答案 → 主进程 ask()
// 立刻取走. 同时只有单 ask, 不变量简化.

(function () {
  'use strict';

  const A = window.AvoxAgent;
  if (!A) {
    console.error('[agent_test] AvoxAgent wrapper not loaded');
    return;
  }

  // ----- 全局状态 -----
  const state = {
    host: null,
    session: null,
    autoStarted: false,         // 是否已自动启动 (host + session 都好了)
    currentAssistantEl: null,
    currentAssistantText: '',
    currentReasoningEl: null,
    currentReasoningText: '',
    todoEl: null,               // 当前 todo 卡片 DOM (todo_write 整表快照, 复用同一张卡更新)
    tokenBuffer: '',            // handleToken 累积的待处理 buffer (跨 token 切 <think>...</think>)
    inThink: false,             // 当前是否在 <think> 块里
    observerHandle: null,
    providers: [],
    selected: null,
    pendingApprovalReq: null,
    pendingApprovalResolve: null,
    settingsOpen: false,
    // 待发送贴图: [{file, previewUrl, name}] (跟随所选模型 imageInput 门控)
    pendingImages: [],
  };

  // ----- DOM 引用 -----
  const $ = (id) => document.getElementById(id);
  const dom = {
    modelSelect:      $('modelSelect'),
    modelCount:       $('modelCount'),
    btnRefreshList:   $('btnRefreshList'),
    hostStatus:       $('hostStatus'),
    sessionStatus:    $('sessionStatus'),
    btnSettings:      $('btnSettings'),
    settingsPanel:    $('settingsPanel'),
    stateError:       $('stateError'),

    sessionInput:     $('sessionInput'),
    btnNewSession:    $('btnNewSession'),
    btnCloseSession:  $('btnCloseSession'),
    btnCompact:       $('btnCompact'),
    btnCancel:        $('btnCancel'),
    btnRestart:       $('btnRestart'),

    skillName:        $('skillNameInput'),
    skillInput:       $('skillInputInput'),
    btnRunSkill:      $('btnRunSkill'),

    eventLog:         $('eventLog'),
    btnReplay:        $('btnReplay'),
    btnClearEvents:   $('btnClearEvents'),
    btnClearMessages: $('btnClearMessages'),

    messages:         $('messages'),
    messagesEmpty:    $('messagesEmpty'),
    inputBox:         $('inputBox'),
    pasteThumbs:      $('pasteThumbs'),
    btnSend:          $('btnSend'),
    btnStop:          $('btnStop'),
    btnSteer:         $('btnSteer'),
    btnInject:        $('btnInject'),

    approvalDialog:   $('approvalDialog'),
    approvalTool:     $('approvalTool'),
    approvalCallId:   $('approvalCallId'),
    approvalReason:   $('approvalReason'),
    btnAllow:         $('btnApprovalAllow'),
    btnReject:        $('btnApprovalReject'),
    btnACancel:       $('btnApprovalCancel'),

    btnEditModels:    $('btnEditModels'),
    modelsEditor:     $('modelsEditor'),
    editorMeta:       $('editorMeta'),
    editorBody:       $('editorBody'),
    editorStatus:     $('editorStatus'),
    btnSaveModels:    $('btnSaveModels'),
    btnRevert:        $('btnRevert'),
    btnCloseEditor:   $('btnCloseEditor'),
  };

  // 编辑器内部状态 (working copy; 不动原 JSON, save 时整体覆盖 user-level 文件)
  // JSON 顶层通常是 {"providers": {...}} 包裹, 也可能直接是 provider 字典 (兼容).
  const editorState = {
    data: null,           // 内部 working copy (顶层直接是 provider 字典)
    raw: null,            // 原始顶层对象, save 时回填用
    wrapperKey: null,     // 顶层包裹键; 无包裹则 null
    filePath: '',
    source: '',
    dirty: false,
    currentPid: null,     // 当前选中的 provider id
    currentMid: null,     // 当前选中的 model id
  };

  // ----- 工具 -----
  function log(level, args) {
    const ev = document.createElement('div');
    ev.className = level === 'error' ? 'error'
      : level === 'success' ? 'success'
      : level === 'tool' ? 'tool'
      : '';
    ev.style.color = level === 'error' ? '#e74c3c'
      : level === 'success' ? '#58d68d'
      : level === 'tool' ? '#f4d03f'
      : '#888';
    const ts = new Date().toTimeString().slice(0, 8);
    ev.textContent = `[${ts}] ${Array.from(args).join(' ')}`;
    dom.eventLog.appendChild(ev);
    while (dom.eventLog.childNodes.length > 200) {
      dom.eventLog.removeChild(dom.eventLog.firstChild);
    }
    dom.eventLog.scrollTop = dom.eventLog.scrollHeight;
  }

  // .msg.* DOM 渲染统一从 chat_render.js 来, 见下方 renderers。
  // 检测 messages 容器当前是否在底部 (阈值内算在底 — 容许亚像素 / 边框抖动).
  function isAtBottom(threshold) {
    const el = dom.messages;
    if (!el) return true;
    const t = threshold == null ? 40 : threshold;
    return el.scrollHeight - el.scrollTop - el.clientHeight < t;
  }
  // 渲染层入口: 集中管理 tool-result 折叠 / todo 卡 / markdown / 工具专属 renderer。
  // 加新工具的专属渲染 = 改 chat_render.js 的 TOOL_RENDERERS 注册表, 不动控制流。
  const renderers = ChatRender.make({ dom, state, isAtBottom });

  function clearMessages() {
    dom.messages.innerHTML = '';
    const e = document.createElement('div');
    e.className = 'empty';
    e.id = 'messagesEmpty';
    e.textContent = '对话已清空';
    dom.messages.appendChild(e);
    dom.messagesEmpty = e;
    state.currentAssistantEl = null;
    state.currentAssistantText = '';
    state.currentReasoningEl = null;
    state.currentReasoningText = '';
    state.tokenBuffer = '';
    state.inThink = false;
  }

  function setHostStatus(label, cls) {
    dom.hostStatus.className = 'pill host ' + (cls || '');
    dom.hostStatus.innerHTML = '<strong>Host:</strong>' + label;
  }
  function setSessionStatus(label, cls) {
    dom.sessionStatus.className = 'pill session ' + (cls || '');
    dom.sessionStatus.innerHTML = '<strong>Session:</strong>' + label;
  }
  function showError(msg) {
    dom.stateError.textContent = msg;
    dom.stateError.hidden = false;
  }
  function hideError() { dom.stateError.hidden = true; }

  function refreshButtons() {
    const hasHost = state.host && !state.host.closed;
    const hasSession = !!state.session;
    dom.btnNewSession.disabled    = !hasHost || hasSession;
    dom.btnCloseSession.disabled  = !hasSession;
    dom.btnCompact.disabled       = !hasSession;
    dom.btnCancel.disabled        = !hasSession;
    dom.btnSend.disabled          = !hasSession;
    dom.btnSteer.disabled         = !hasSession;
    dom.btnInject.disabled        = !hasSession;
    dom.btnRestart.disabled       = !hasHost;
    dom.inputBox.disabled         = !hasSession;
    syncRunStopButtons();
  }

  // 同步发送/停止按钮的状态: 始终都显示, 只切 disabled. (让用户始终能看见取消按钮就在发送按钮右边)
  // btnSend 在 running 时也 disable — 原行为是 hidden, 改成 disable 保留「不可点」语义.
  function syncRunStopButtons() {
    const running = !!state.session && state.session.status() === 1;
    dom.btnStop.disabled = !running;
    if (running) dom.btnSend.disabled = true;
  }

  // ----- 模型下拉 -----
  function entryKey(p) { return p.provider + '/' + p.model; }

  function renderModelSelect() {
    dom.modelSelect.innerHTML = '';
    if (state.providers.length === 0) {
      const opt = document.createElement('option');
      opt.value = '';
      opt.textContent = 'providers.json 加载失败或为空';
      dom.modelSelect.appendChild(opt);
      dom.modelSelect.disabled = true;
      dom.modelCount.textContent = '0 条';
      return;
    }
    dom.modelSelect.disabled = false;
    for (const p of state.providers) {
      const opt = document.createElement('option');
      opt.value = entryKey(p);
      const tags = [];
      if (p.free) tags.push('FREE');
      if (p.needsApiKey) tags.push('NEEDS KEY');
      else if (p.ready) tags.push('READY');
      if (p.imageInput) tags.push('VISION');
      let label = entryKey(p);
      if (tags.length) label += ' · ' + tags.join(' · ');
      if (p.contextWindow && p.contextWindow > 0) {
        label += ' · ' + (p.contextWindow >= 1024
          ? Math.round(p.contextWindow / 1024) + 'k'
          : p.contextWindow);
      }
      opt.textContent = label;
      if (p.needsApiKey) opt.disabled = true;
      if (state.selected && entryKey(state.selected) === entryKey(p)) opt.selected = true;
      dom.modelSelect.appendChild(opt);
    }
    dom.modelCount.textContent = state.providers.length + ' 条';
  }

  function selectProvider(p) {
    const changed = !state.selected || entryKey(state.selected) !== entryKey(p);
    state.selected = p;
    if (p) dom.modelSelect.value = entryKey(p);
    try {
      if (p) localStorage.setItem('avox_agent.selectedModel', entryKey(p));
      else   localStorage.removeItem('avox_agent.selectedModel');
    } catch (_) {}
    log('in', ['已选择', p.provider + '/' + p.model,
              p.ready ? '(ready)' : (p.needsApiKey ? '(needs key)' : '')]);
    // 选择变化 → 重启 host + session. 已选好一次就别再弹错.
    if (changed && state.autoStarted) restartAgent();
  }

  function loadSavedModelKey() {
    try { return localStorage.getItem('avox_agent.selectedModel') || ''; }
    catch (_) { return ''; }
  }

  function refreshProviderList() {
    dom.modelSelect.disabled = true;
    dom.modelSelect.innerHTML = '<option>加载中…</option>';
    const r = A.listProviders();
    if (r.error) {
      log('error', ['listProviders 失败:', r.error]);
      dom.modelSelect.innerHTML = '<option>加载失败: ' + escapeHtml(r.error) + '</option>';
      dom.modelSelect.disabled = true;
      showError('加载 providers.json 失败: ' + r.error);
      return;
    }
    state.providers = r.entries || [];
    log('success', ['/list 返回', state.providers.length, '条']);
    if (state.providers.length > 0) {
      const savedKey = loadSavedModelKey();
      const savedHit = savedKey
        ? state.providers.find((p) => entryKey(p) === savedKey && !p.needsApiKey)
        : null;
      if (savedHit) {
        state.selected = savedHit;
        log('in', ['已恢复上次选择的模型', savedHit.provider + '/' + savedHit.model]);
      } else {
        const readyIdx = state.providers.findIndex((p) => p.ready);
        const fallbackIdx = state.providers.findIndex((p) => !p.needsApiKey);
        const pickIdx = readyIdx >= 0 ? readyIdx : (fallbackIdx >= 0 ? fallbackIdx : 0);
        state.selected = state.providers[pickIdx];
      }
    } else {
      state.selected = null;
    }
    renderModelSelect();
    refreshButtons();
  }

  function escapeHtml(s) {
    return String(s).replace(/[&<>"']/g, (c) => ({
      '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;',
    }[c]));
  }

  function buildConfigFromSelection() {
    const p = state.selected;
    if (!p) throw new Error('未选模型');
    return {
      provider: p.provider,
      model: p.model,
      maxTokens: 16384,
      maxParallelToolCalls: 5,
      persona: '',
      agentInstructions: { enabled: true },
    };
  }

  // ----- 下拉交互 -----
  dom.modelSelect.addEventListener('change', () => {
    const key = dom.modelSelect.value;
    const p = state.providers.find((p) => entryKey(p) === key);
    if (p) selectProvider(p);
  });
  dom.btnRefreshList.addEventListener('click', () => {
    refreshProviderList();
    if (state.selected && !state.autoStarted) autoStart();
  });

  // ----- 设置面板折叠 -----
  dom.btnSettings.addEventListener('click', () => {
    state.settingsOpen = !state.settingsOpen;
    dom.settingsPanel.hidden = !state.settingsOpen;
    dom.btnSettings.classList.toggle('active', state.settingsOpen);
  });

  // ----- Provider/Model 编辑器 -----
  //
  // JSON 格式 (与 assets/config/providers.json 同构):
  //   {
  //     "openai": {
  //       "apiUrl": "https://api.openai.com/v1",
  //       "apiPath": "/chat/completions",
  //       "apiKey": "sk-...",
  //       "models": {
  //         "gpt-4o": { "chat": true, "contextWindow": 128000, "imageInput": true }
  //       }
  //     }
  //   }
  //
  // 编辑的是 working copy, save 整体写回 user-level (C++ AssetLoader 优先读它).

  function setEditorStatus(msg, cls) {
    dom.editorStatus.textContent = msg || '';
    dom.editorStatus.style.color = cls === 'error' ? '#e74c3c'
      : cls === 'success' ? '#58d68d'
      : '#888';
  }
  function markDirty() {
    editorState.dirty = true;
    setEditorStatus('有未保存的改动');
  }

  function loadEditorFromDisk() {
    setEditorStatus('加载中…');
    const r = window.avox.loadProvidersJson();
    if (!r.ok) {
      setEditorStatus('加载失败: ' + r.error, 'error');
      return;
    }
    let parsed;
    try { parsed = JSON.parse(r.json); }
    catch (e) {
      setEditorStatus('JSON 解析失败: ' + e.message, 'error');
      return;
    }
    if (!parsed || typeof parsed !== 'object' || Array.isArray(parsed)) {
      setEditorStatus('不是对象顶层结构', 'error');
      return;
    }
    // 检测顶层是否有 "providers" 包裹层; 是则解一层.
    let wrapperKey = null;
    let data = parsed;
    const keys = Object.keys(parsed);
    if (keys.length === 1 && keys[0] === 'providers'
        && parsed.providers && typeof parsed.providers === 'object'
        && !Array.isArray(parsed.providers)) {
      wrapperKey = 'providers';
      data = parsed.providers;
    }
    editorState.raw = parsed;
    editorState.wrapperKey = wrapperKey;
    editorState.data = data;
    editorState.filePath = r.path;
    editorState.source = r.source;
    editorState.dirty = false;
    editorState.view = 'list';
    editorState.currentPid = null;
    editorState.currentMid = null;
    dom.editorMeta.textContent =
      (r.source === 'user' ? '当前来源: user-level' : '当前来源: assets (默认)')
      + ' · ' + r.path;
    setEditorStatus('');
    renderEditor();
  }

  function openEditor() {
    dom.modelsEditor.classList.add('shown');
    loadEditorFromDisk();
  }
  async function closeEditor() {
    if (editorState.dirty) {
      const ok = await htmlConfirm({
        title: '关闭编辑器',
        message: '有未保存的改动, 确认关闭? 改动会丢失。',
        okLabel: '关闭',
        danger: true,
      });
      if (!ok) return;
    }
    dom.modelsEditor.classList.remove('shown');
  }

  // 保留未知字段, 只对已知字段填默认. 不修改用户已写的值.
  function ensureProviderShape(p) {
    if (!p || typeof p !== 'object' || Array.isArray(p)) p = {};
    if (typeof p.apiUrl !== 'string') p.apiUrl = '';
    if (typeof p.apiPath !== 'string') p.apiPath = '/chat/completions';
    if (typeof p.apiKey !== 'string') p.apiKey = '';
    if (!p.models || typeof p.models !== 'object' || Array.isArray(p.models)) p.models = {};
    return p;
  }
  function ensureModelShape(m) {
    if (!m || typeof m !== 'object' || Array.isArray(m)) m = {};
    if (typeof m.chat !== 'boolean') m.chat = true;
    if (typeof m.contextWindow !== 'number') m.contextWindow = 0;
    if (typeof m.imageInput !== 'boolean') m.imageInput = false;
    if (typeof m.imageOutput !== 'boolean') m.imageOutput = false;
    if (typeof m.multiImage !== 'boolean') m.multiImage = false;
    // name / displayName 都接受 (displayName 兼容历史)
    if (typeof m.name !== 'string' && typeof m.displayName === 'string') m.name = m.displayName;
    return m;
  }

  // HTML 弹层工具 — 替代 Electron renderer 默认禁用的 prompt/confirm/alert.
  function htmlConfirm(opts) {
    return new Promise((resolve) => {
      const dlg = document.createElement('div');
      dlg.className = 'modal-overlay';
      dlg.innerHTML =
        '<div class="prompt-box">' +
          '<h4>' + escapeHtml(opts.title || '') + '</h4>' +
          '<div class="prompt-msg">' + escapeHtml(opts.message || '') + '</div>' +
          '<div class="prompt-actions">' +
            '<button type="button" class="cancel">取消</button>' +
            '<button type="button" class="' + (opts.danger ? 'danger' : 'primary') + ' ok">' + escapeHtml(opts.okLabel || '确定') + '</button>' +
          '</div>' +
        '</div>';
      document.body.appendChild(dlg);
      const ctrl = new AbortController();
      const close = (v) => { ctrl.abort(); dlg.remove(); resolve(v); };
      dlg.querySelector('.ok').onclick = () => close(true);
      dlg.querySelector('.cancel').onclick = () => close(false);
      dlg.addEventListener('click', (e) => {
        if (e.target === dlg) close(false);
      }, { signal: ctrl.signal });
      document.addEventListener('keydown', (e) => {
        if (e.key === 'Enter') { e.preventDefault(); close(true); }
        else if (e.key === 'Escape') { e.preventDefault(); close(false); }
      }, { signal: ctrl.signal });
    });
  }
  function htmlAlert(title, message) {
    return new Promise((resolve) => {
      const dlg = document.createElement('div');
      dlg.className = 'modal-overlay';
      dlg.innerHTML =
        '<div class="prompt-box">' +
          '<h4>' + escapeHtml(title || '') + '</h4>' +
          '<div class="prompt-msg">' + escapeHtml(message || '') + '</div>' +
          '<div class="prompt-actions">' +
            '<button type="button" class="primary ok">确定</button>' +
          '</div>' +
        '</div>';
      document.body.appendChild(dlg);
      const ctrl = new AbortController();
      const close = () => { ctrl.abort(); dlg.remove(); resolve(); };
      dlg.querySelector('.ok').onclick = close;
      dlg.addEventListener('click', (e) => {
        if (e.target === dlg) close();
      }, { signal: ctrl.signal });
      document.addEventListener('keydown', (e) => {
        if (e.key === 'Enter' || e.key === 'Escape') { e.preventDefault(); close(); }
      }, { signal: ctrl.signal });
    });
  }

  // —— 编辑器主渲染 ——
  function renderEditor() {
    // 先把非「当前正在编辑」的 pending 占位项清掉 ——
    // 1) 用户在 creating 模式下点下拉里已有项 = onSwitch 切换到真实项, pending 变孤儿;
    // 2) 用户连点多次「+」= 留下多个 pending, 老的全部无用;
    // 3) renderEditor 多次触发都安全.
    const activePendingPid = isPendingId(editorState.currentPid) ? editorState.currentPid : null;
    const activePendingMid = isPendingId(editorState.currentMid) ? editorState.currentMid : null;
    for (const pid of Object.keys(editorState.data)) {
      if (isPendingId(pid) && pid !== activePendingPid) delete editorState.data[pid];
    }
    if (activePendingPid && editorState.data[activePendingPid]) {
      const p = editorState.data[activePendingPid];
      for (const mid of Object.keys(p.models || {})) {
        if (isPendingId(mid) && mid !== activePendingMid) delete p.models[mid];
      }
    }
    dom.editorBody.innerHTML = '';
    if (!editorState.data) return;
    // 只把真实 id 算进选择候选, pending 不参与默认 selection 推断.
    const ids = Object.keys(editorState.data).filter((id) => !isPendingId(id));
    // 维持 selection 落在真实存在项上; 缺则回退到第一条 / 清空.
    if (ids.length === 0 && !activePendingPid) {
      editorState.currentPid = null;
      editorState.currentMid = null;
    } else {
      if (!editorState.currentPid || !editorState.data[editorState.currentPid]) {
        editorState.currentPid = ids[0] || activePendingPid;
        editorState.currentMid = null;
      }
      const p = editorState.data[editorState.currentPid];
      if (p) {
        const mids = Object.keys(p.models || {}).filter((mid) => !isPendingId(mid));
        if (!editorState.currentMid || !p.models[editorState.currentMid]) {
          editorState.currentMid = mids[0] || activePendingMid || null;
        }
      }
    }
    renderProviderSection();
    if (editorState.currentPid) renderModelSection();
    if (ids.length === 0 && !activePendingPid) {
      const e = document.createElement('div');
      e.className = 'editor-empty';
      e.textContent = '空配置 — 点 Provider 旁的「+」新建';
      dom.editorBody.appendChild(e);
    }
  }

  // —— 通用控件工厂 ——
  function detailRow(labelText, fieldEl) {
    const r = document.createElement('div');
    r.className = 'detail-row';
    const l = document.createElement('label');
    l.textContent = labelText;
    const f = document.createElement('div');
    f.className = 'detail-field';
    f.appendChild(fieldEl);
    r.appendChild(l);
    r.appendChild(f);
    return r;
  }
  function makeTextInput(value, placeholder, onChange) {
    const i = document.createElement('input');
    i.type = 'text';
    i.placeholder = placeholder || '';
    i.value = value == null ? '' : value;
    i.addEventListener('input', () => onChange(i.value));
    return i;
  }
  function makePasswordInput(value, placeholder, onChange) {
    const wrap = document.createElement('div');
    wrap.className = 'apikey-wrap';
    const i = document.createElement('input');
    i.type = 'password';
    i.placeholder = placeholder || '';
    i.value = value || '';
    i.addEventListener('input', () => onChange(i.value));
    const eye = document.createElement('button');
    eye.type = 'button';
    eye.textContent = '👁';
    eye.title = '显示/隐藏';
    eye.addEventListener('click', () => {
      i.type = i.type === 'password' ? 'text' : 'password';
    });
    wrap.appendChild(i);
    wrap.appendChild(eye);
    return wrap;
  }
  function makeIconBtn(icon, title, onClick) {
    const b = document.createElement('button');
    b.type = 'button';
    b.className = 'icon';
    b.textContent = icon;
    b.title = title;
    b.addEventListener('click', onClick);
    return b;
  }
  // 可编辑下拉: 顶部输入框 + 折叠式选项列表 (focus 时展开, blur 时收起).
// 选已有项=切换, 输新 id=重命名, 输入时按子串过滤选项.
// opts.creating: 「新建」模式 — input 显示为空 + placeholder, 失焦空=取消, 失焦非空=把占位条目改名.
// opts.onCancelCreating: creating 模式下, 失焦空时调 (清理占位条目 + 重置 selection).
  function makeEditableSelect(options, currentValue, onSwitch, onRename, opts) {
    const wrap = document.createElement('div');
    wrap.className = 'editable-select';
    const creating = !!(opts && opts.creating);
    const onCancelCreating = (opts && opts.onCancelCreating) || (() => {});

    const input = document.createElement('input');
    input.type = 'text';
    input.value = creating ? '' : (currentValue || '');
    input.placeholder = creating
      ? (opts.placeholder || '输入新 id, Enter 确认, Esc 取消')
      : (options.length === 0 ? '— 空 —' : '选择或输入新 id');
    input.autocomplete = 'off';
    input.spellcheck = false;
    // 未选模型 / 未点 + 时, input 只读 — 强制走「点 + 起名」流程, 避免误 rename.
    input.readOnly = !creating && !currentValue;
    wrap.appendChild(input);

    const list = document.createElement('div');
    list.className = 'es-list collapsed';
    if (options.length === 0) {
      const empty = document.createElement('div');
      empty.className = 'es-empty';
      empty.textContent = '空';
      list.appendChild(empty);
    } else {
      for (const o of options) {
        const item = document.createElement('div');
        item.className = 'es-item';
        item.textContent = o.label || o.value;
        item.dataset.value = o.value;
        if (o.value === currentValue) item.classList.add('cur');
        item.addEventListener('mousedown', (e) => {
          // mousedown 先于 input.blur, 防止先触发 change 回滚.
          e.preventDefault();
          if (o.value !== currentValue) {
            input.value = o.value;
            onSwitch(o.value);
          }
          input.blur();
        });
        list.appendChild(item);
      }
    }
    wrap.appendChild(list);

    // focus 展开, blur 收起 (用微延迟让 item 的 mousedown 先跑完).
    input.addEventListener('focus', () => list.classList.remove('collapsed'));
    input.addEventListener('blur', () => {
      setTimeout(() => list.classList.add('collapsed'), 0);
    });

    // 输入时按子串过滤 (空串=全部显示).
    input.addEventListener('input', () => {
      const v = input.value.trim().toLowerCase();
      for (const item of list.querySelectorAll('.es-item')) {
        const match = !v || item.dataset.value.toLowerCase().includes(v);
        item.style.display = match ? '' : 'none';
      }
    });

    // Esc 在 creating 模式下取消.
    input.addEventListener('keydown', (e) => {
      if (e.key === 'Escape' && creating) {
        e.preventDefault();
        e.stopPropagation();
        onCancelCreating();
        input.blur();
      }
    });

    // 失焦 commit: 匹配现有=切换, 不匹配=重命名 (creating 模式下空=取消).
    input.addEventListener('change', () => {
      const v = input.value.trim();
      if (!v) {
        if (creating) {
          onCancelCreating();
        } else {
          input.value = currentValue || '';
        }
        return;
      }
      const matched = options.find((o) => o.value === v);
      if (matched) {
        if (matched.value !== currentValue) onSwitch(matched.value);
        else input.value = currentValue || '';
      } else {
        const ok = onRename(v);
        if (!ok) input.value = creating ? '' : (currentValue || '');
      }
    });

    return wrap;
  }
  function genUniqueId(prefix, existing) {
    let n = 1;
    while (existing[prefix + '-' + n]) n++;
    return prefix + '-' + n;
  }
  // 焦点跳到 editorBody 内第 idx 个 editable-select 的 input, 全选文本方便覆盖输入.
  function focusEditableSelect(idx) {
    const inputs = dom.editorBody.querySelectorAll('.editable-select input');
    if (inputs[idx]) {
      inputs[idx].focus();
      inputs[idx].select();
    }
  }
  // 强制展开第 idx 个 .editable-select 的下拉列表 (用户刚提交了一个新条目, 让他看见).
  function expandListAt(idx) {
    const lists = dom.editorBody.querySelectorAll('.editable-select .es-list');
    if (lists[idx]) lists[idx].classList.remove('collapsed');
  }

  // —— Provider 段 ——
  function renderProviderSection() {
    const sec = document.createElement('div');
    sec.className = 'editor-section';

    // 排除正在创建中的占位项, 不要让它出现在下拉列表里.
    const ids = Object.keys(editorState.data).filter((id) => !isPendingId(id));
    const isCreating = isPendingId(editorState.currentPid);
    const head = document.createElement('div');
    head.className = 'section-head';

    const label = document.createElement('span');
    label.className = 'section-label';
    label.textContent = 'Provider';
    head.appendChild(label);

    head.appendChild(makeEditableSelect(
      ids.map((id) => ({ value: id, label: id })),
      editorState.currentPid,
      (v) => {
        editorState.currentPid = v;
        editorState.currentMid = null;
        renderEditor();
      },
      (newPid) => applyRenameProvider(editorState.currentPid, newPid),
      isCreating ? {
        creating: true,
        placeholder: '✎ 输入新 provider id, Enter 确认, Esc 取消',
        onCancelCreating: cancelCreatingProvider,
      } : undefined
    ));
    if (editorState.currentPid && !isCreating) {
      head.appendChild(makeIconBtn('🗑', '删除 provider',
        () => deleteProvider(editorState.currentPid)));
    }
    head.appendChild(makeIconBtn('+', '新建 provider', addProvider));
    sec.appendChild(head);

    // creating 模式也显示 detail card (用户先填字段再起名也行, pending 条目里字段照样写入,
    // 改名 / 保存后保留, 取消时整体清掉 — 不会出现「点 + 后内容消失」的错觉).
    if (editorState.currentPid) {
      const p = ensureProviderShape(editorState.data[editorState.currentPid]);
      const card = document.createElement('div');
      card.className = 'detail-card';

      card.appendChild(detailRow('apiUrl',
        makeTextInput(p.apiUrl, 'https://api.openai.com/v1',
          (v) => { p.apiUrl = v; markDirty(); })));
      card.appendChild(detailRow('apiPath',
        makeTextInput(p.apiPath, '/chat/completions',
          (v) => { p.apiPath = v; markDirty(); })));
      card.appendChild(detailRow('apiKey',
        makePasswordInput(p.apiKey, 'sk-...',
          (v) => { p.apiKey = v; markDirty(); })));

      sec.appendChild(card);
    }

    dom.editorBody.appendChild(sec);
  }

  // —— Model 段 ——
  function renderModelSection() {
    const sec = document.createElement('div');
    sec.className = 'editor-section';

    const p = ensureProviderShape(editorState.data[editorState.currentPid]);
    // 排除正在创建中的占位项.
    const mids = Object.keys(p.models).filter((mid) => !isPendingId(mid));
    const isCreating = isPendingId(editorState.currentMid);
    const head = document.createElement('div');
    head.className = 'section-head';

    const label = document.createElement('span');
    label.className = 'section-label';
    label.textContent = 'Model';
    head.appendChild(label);

    head.appendChild(makeEditableSelect(
      mids.map((mid) => ({
        value: mid,
        label: mid + (p.models[mid].name ? ' · ' + p.models[mid].name : ''),
      })),
      editorState.currentMid,
      (v) => {
        editorState.currentMid = v;
        renderEditor();
      },
      (newMid) => applyRenameModel(editorState.currentPid, editorState.currentMid, newMid),
      isCreating ? {
        creating: true,
        placeholder: '✎ 输入新 model id, Enter 确认, Esc 取消',
        onCancelCreating: cancelCreatingModel,
      } : undefined
    ));
    if (editorState.currentMid && !isCreating) {
      head.appendChild(makeIconBtn('🗑', '删除模型',
        () => deleteModel(editorState.currentPid, editorState.currentMid)));
    }
    head.appendChild(makeIconBtn('+', '新建模型', addModel));
    sec.appendChild(head);

    // creating 模式也显示 detail card (字段填进去就写到 pending 条目里, 改名 / 保存后保留,
    // 取消时整体清掉 — 不会出现「点 + 后内容消失」的错觉).
    if (editorState.currentMid) {
      const m = ensureModelShape(p.models[editorState.currentMid]);
      const card = document.createElement('div');
      card.className = 'detail-card';

      card.appendChild(detailRow('name (展示名)',
        makeTextInput(m.name, '可选, 如 DeepSeek Pro',
          (v) => { m.name = v; markDirty(); })));

      const ctxIn = document.createElement('input');
      ctxIn.type = 'number';
      ctxIn.min = '0';
      ctxIn.placeholder = '0 = 未知';
      ctxIn.value = m.contextWindow || 0;
      ctxIn.addEventListener('input', () => {
        m.contextWindow = parseInt(ctxIn.value, 10) || 0;
        markDirty();
      });
      card.appendChild(detailRow('contextWindow', ctxIn));

      const flagsField = document.createElement('div');
      flagsField.className = 'flags';
      const mkCheck = (key, label) => {
        const lab = document.createElement('label');
        lab.className = 'check';
        const cb = document.createElement('input');
        cb.type = 'checkbox';
        cb.checked = !!m[key];
        cb.addEventListener('change', () => { m[key] = cb.checked; markDirty(); });
        lab.appendChild(cb);
        lab.appendChild(document.createTextNode(label));
        return lab;
      };
      flagsField.appendChild(mkCheck('chat', 'chat'));
      flagsField.appendChild(mkCheck('imageInput', 'vision'));
      flagsField.appendChild(mkCheck('imageOutput', 'gen'));
      flagsField.appendChild(mkCheck('multiImage', 'multi'));
      card.appendChild(detailRow('flags', flagsField));

      card.appendChild(detailRow('apiPath 覆盖',
        makeTextInput(m.apiPath, '留空用 provider 的',
          (v) => {
            if (v && v.trim()) m.apiPath = v.trim();
            else delete m.apiPath;
            markDirty();
          })));

      // 推理档位按模型配: 各厂商合法值不一, zhipu 的 glm-5.3(-flash) 只认 low/high/max.
      card.appendChild(detailRow('reasoningEffort',
        makeTextInput(m.reasoningEffort || '', '留空用全局默认, 如 low/high/max',
          (v) => {
            const t = v.trim();
            if (t) m.reasoningEffort = t;
            else delete m.reasoningEffort;
            markDirty();
          })));

      sec.appendChild(card);
    }

    dom.editorBody.appendChild(sec);
  }

  // —— 操作 ——
  // 用一个唯一的「待命名」占位 id (避免多次点 + 撞名 + 区分正在编辑的临时项).
  let pendingSeq = 0;
  function nextPendingId(prefix) {
    pendingSeq++;
    return '__pending_' + prefix + '_' + Date.now() + '_' + pendingSeq + '__';
  }
  function isPendingId(id) {
    return typeof id === 'string' && id.indexOf('__pending_') === 0;
  }
  function addProvider() {
    const pendingId = nextPendingId('provider');
    editorState.data[pendingId] = ensureProviderShape({
      apiUrl: 'https://api.openai.com/v1',
      apiPath: '/chat/completions',
      apiKey: '',
      models: {},
    });
    editorState.currentPid = pendingId;
    editorState.currentMid = null;
    markDirty();
    renderEditor();
    requestAnimationFrame(() => focusEditableSelect(0));
  }
  function cancelCreatingProvider() {
    const pending = editorState.currentPid;
    if (isPendingId(pending) && editorState.data[pending]) {
      delete editorState.data[pending];
    }
    const remaining = Object.keys(editorState.data);
    editorState.currentPid = remaining[0] || null;
    editorState.currentMid = null;
    markDirty();
    renderEditor();
  }
  // 重命名 provider: 直接同步改 working copy, 校验失败弹 HTML 弹层.
  // 返回 true=成功, false=失败 (调用方需把 input.value 回滚到 currentPid).
  // id 白名单: 除字母数字_- 外放开 . : / — 真实模型名常带点 (glm-5.3-flash),
  // 冒号 (ollama 的 qwen2.5:7b), 斜杠 (openrouter 的 deepseek/deepseek-chat). id 只做
  // 字典键和 entryKey 拼接, 从不参与路径/URL 拆分, 放宽无副作用.
  var ID_RE = /^[a-zA-Z0-9_.:/-]+$/;
  function applyRenameProvider(oldPid, newPid) {
    if (!newPid || newPid === oldPid) return false;
    if (!ID_RE.test(newPid)) {
      htmlAlert('格式错误', 'provider id 只能是字母数字_-.:/, 实际: ' + newPid);
      return false;
    }
    if (editorState.data[newPid]) {
      htmlAlert('已存在', 'provider ' + newPid + ' 已存在');
      return false;
    }
    editorState.data[newPid] = editorState.data[oldPid];
    delete editorState.data[oldPid];
    editorState.currentPid = newPid;
    markDirty();
    renderEditor();
    // 展开 provider 下拉, 让用户看见刚提交的新条目.
    requestAnimationFrame(() => expandListAt(0));
    return true;
  }
  async function deleteProvider(pid) {
    const ok = await htmlConfirm({
      title: '删除 provider',
      message: '确认删除 "' + pid + '" 及下属所有模型?',
      okLabel: '删除',
      danger: true,
    });
    if (!ok) return;
    delete editorState.data[pid];
    const remaining = Object.keys(editorState.data);
    editorState.currentPid = remaining[0] || null;
    editorState.currentMid = null;
    markDirty();
    renderEditor();
  }

  function addModel() {
    const p = ensureProviderShape(editorState.data[editorState.currentPid]);
    const pendingId = nextPendingId('model');
    p.models[pendingId] = ensureModelShape({});
    editorState.currentMid = pendingId;
    markDirty();
    renderEditor();
    // 焦点落到 model 下拉 (第 1 个 section 是 provider, 第 2 个是 model).
    requestAnimationFrame(() => focusEditableSelect(1));
  }
  function cancelCreatingModel() {
    const pid = editorState.currentPid;
    const pending = editorState.currentMid;
    if (pid && editorState.data[pid]) {
      if (isPendingId(pending) && editorState.data[pid].models[pending]) {
        delete editorState.data[pid].models[pending];
      }
      const remaining = Object.keys(editorState.data[pid].models);
      editorState.currentMid = remaining[0] || null;
    }
    markDirty();
    renderEditor();
  }
  function applyRenameModel(pid, oldMid, newMid) {
    if (!newMid || newMid === oldMid) return false;
    if (!ID_RE.test(newMid)) {
      htmlAlert('格式错误', 'model id 只能是字母数字_-.:/, 实际: ' + newMid);
      return false;
    }
    const p = editorState.data[pid];
    if (p.models[newMid]) {
      htmlAlert('已存在', 'model ' + pid + '/' + newMid + ' 已存在');
      return false;
    }
    p.models[newMid] = p.models[oldMid];
    delete p.models[oldMid];
    editorState.currentMid = newMid;
    markDirty();
    renderEditor();
    // 展开 model 下拉, 让用户看见刚提交的新条目.
    requestAnimationFrame(() => expandListAt(1));
    return true;
  }
  async function deleteModel(pid, mid) {
    const ok = await htmlConfirm({
      title: '删除模型',
      message: '确认删除 ' + pid + '/' + mid + '?',
      okLabel: '删除',
      danger: true,
    });
    if (!ok) return;
    delete editorState.data[pid].models[mid];
    const remaining = Object.keys(editorState.data[pid].models);
    editorState.currentMid = remaining[0] || null;
    markDirty();
    renderEditor();
  }

  function saveEditor() {
    if (!editorState.data) return;
    // 未命名的 pending 条目自动取个唯一 id (用户可能只填字段没起名, 别把数据丢掉).
    const clean = {};
    // 跟踪当前 pending 条目被改成什么名, 之后同步给 currentPid/currentMid.
    let renamedPid = null;
    let renamedMid = null;
    for (const pid of Object.keys(editorState.data)) {
      if (isPendingId(pid)) {
        const newPid = genUniqueId('provider', editorState.data);
        clean[newPid] = editorState.data[pid];
        if (pid === editorState.currentPid) renamedPid = newPid;
      } else {
        clean[pid] = editorState.data[pid];
      }
      const p = clean[pid];
      for (const mid of Object.keys(p.models || {})) {
        if (isPendingId(mid)) {
          const newMid = genUniqueId('model', p.models);
          p.models[newMid] = p.models[mid];
          if (pid === editorState.currentPid && mid === editorState.currentMid) renamedMid = newMid;
        }
      }
    }
    // 同步回 editorState (内存) — 让当前选中状态跟磁盘一致, 避免 UI 还指向 pending.
    editorState.data = clean;
    if (renamedPid) editorState.currentPid = renamedPid;
    if (renamedMid) editorState.currentMid = renamedMid;
    setEditorStatus('保存中…');
    let json;
    try {
      const top = editorState.wrapperKey ? { [editorState.wrapperKey]: clean } : clean;
      json = JSON.stringify(top, null, 2);
    } catch (e) {
      setEditorStatus('序列化失败: ' + e.message, 'error');
      return;
    }
    const r = window.avox.saveProvidersJson(json);
    if (!r.ok) {
      setEditorStatus('保存失败: ' + r.error, 'error');
      return;
    }
    editorState.dirty = false;
    editorState.source = 'user';
    editorState.filePath = r.path;
    dom.editorMeta.textContent = '当前来源: user-level · ' + r.path;
    setEditorStatus('已保存 · ' + r.path, 'success');
    refreshProviderList();
    if (!state.autoStarted) autoStart();
  }

  async function revertEditor() {
    if (editorState.dirty) {
      const ok = await htmlConfirm({
        title: '放弃改动',
        message: '有未保存的改动, 确认重新从磁盘加载? 改动会丢失。',
        okLabel: '放弃改动',
        danger: true,
      });
      if (!ok) return;
    }
    loadEditorFromDisk();
  }

  dom.btnEditModels.addEventListener('click', openEditor);
  dom.btnCloseEditor.addEventListener('click', closeEditor);
  dom.btnSaveModels.addEventListener('click', saveEditor);
  dom.btnRevert.addEventListener('click', revertEditor);
  dom.modelsEditor.addEventListener('click', (e) => {
    if (e.target === dom.modelsEditor) closeEditor();
  });

  // ----- Host / Session 自动启动 -----
  async function autoStart() {
    if (state.autoStarted) return;
    if (!state.selected) {
      showError('未选模型 — 刷新 /list 或手动选一下');
      setHostStatus('未就绪', 'error');
      return;
    }
    if (state.selected.needsApiKey) {
      showError('当前模型需要 API key, 不可直接启动');
      setHostStatus('NEEDS KEY', 'error');
      return;
    }
    state.autoStarted = true;
    await ensureHost();
    if (state.host) await ensureSession();
  }

  function ensureHost() {
    return new Promise((resolve) => {
      if (state.host && !state.host.closed) { resolve(); return; }
      let cfg;
      try { cfg = buildConfigFromSelection(); }
      catch (e) { showError(e.message); setHostStatus('未就绪', 'error'); resolve(); return; }
      log('in', ['createAgentHost provider=' + cfg.provider + ' model=' + cfg.model]);
      const r = window.avox.createAgentHost(cfg);
      if (!r || !r.ok) {
        showError('createAgentHost 失败: ' + (r && r.error));
        setHostStatus('未就绪', 'error');
        state.autoStarted = false;
        resolve();
        return;
      }
      state.host = new A.AgentHost();
      state.host.setApprovalUi(new A.ApprovalUi(askApprovalLog));
      hideError();
      setHostStatus('已就绪', 'ready');
      const label = cfg.provider + '/' + cfg.model;
      renderers.appendMessage('system', null, '● host 已创建 · ' + label);
      log('success', ['host 已创建', label]);
      refreshButtons();
      resolve();
    });
  }

  function ensureSession(sid) {
    return new Promise((resolve) => {
      if (!state.host || state.host.closed) { resolve(); return; }
      if (state.session) { resolve(); return; }
      let r;
      try { r = window.avox.hostOpenAgent(sid || ''); }
      catch (e) { r = { ok: false, error: e.message }; }
      if (!r || !r.ok) {
        showError('openAgent 失败: ' + (r && r.error));
        setSessionStatus('未打开', 'error');
        resolve();
        return;
      }
      const session = new A.AgentSession(state.host, r.trackPath || '');
      const obsR = window.avox.sessionAddObserver({
        onSessionEvent: (seq, typeName, evJson) => handleSessionEvent(seq, typeName, evJson),
        onToken:        (text)                    => handleToken(text),
        onReasoning:    (text)                    => handleReasoning(text),
        onToolCall:     (name, argsJson, callId)  => handleToolCall(name, argsJson, callId),
        onToolResult:   (name, result, ok, callId) => handleToolResult(name, result, ok, callId),
        onTurnEnd:      (content, error)          => handleTurnEnd(content, error),
        onStatus:       (status)                  => handleStatus(status),
      });
      if (obsR && obsR.ok) state.observerHandle = obsR.handle;
      else log('error', ['addObserver 失败:', obsR && obsR.error]);
      state.session = session;
      const label = sid ? '已 resume ' + sid : '新建会话';
      setSessionStatus(label, 'ready');
      renderers.appendMessage('system', null, '● session ' + label + ' · ' + r.trackPath);
      log('success', ['session 已打开 track=', r.trackPath]);
      setStatusFlag(false);
      refreshButtons();
      resolve();
    });
  }

  async function restartAgent() {
    log('in', ['restartAgent']);
    if (state.session) {
      try { window.avox.hostCloseAgent(); } catch (_) {}
      state.session = null;
      state.observerHandle = null;
    }
    if (state.host) {
      try { window.avox.hostShutdown(); } catch (_) {}
      state.host = null;
    }
    setHostStatus('重启中', '');
    setSessionStatus('重启中', '');
    state.autoStarted = false;
    await ensureHost();
    if (state.host) await ensureSession();
  }

  function setStatusFlag(running) {
    if (running) setSessionStatus('运行中', 'running');
    else if (state.session) setSessionStatus('已打开', 'ready');
    syncRunStopButtons();
  }

  // ----- 设置面板里的手动按钮 -----
  dom.btnNewSession.addEventListener('click', () => {
    if (!state.host || state.session) return;
    const sid = dom.sessionInput.value.trim() || null;
    ensureSession(sid || '');
  });
  dom.btnCloseSession.addEventListener('click', () => {
    if (!state.session) return;
    try { window.avox.hostCloseAgent(); } catch (_) {}
    state.session = null;
    state.observerHandle = null;
    setSessionStatus('已关闭', '');
    log('success', ['session 已关闭']);
    refreshButtons();
  });
  dom.btnCompact.addEventListener('click', () => {
    if (!state.session) return;
    const ok = state.session.compactNow();
    log(ok ? 'in' : 'error', ['compactNow:', ok ? '已排入' : '未受理']);
  });
  dom.btnCancel.addEventListener('click', () => {
    if (!state.session) return;
    state.session.cancel(A.CancelCause.USER);
    log('warn', ['cancel(user) 已发出']);
  });
  dom.btnRestart.addEventListener('click', () => restartAgent());

  // ----- 输入区 -----
  // —— 贴图: Win+Shift+S → Ctrl+V 直达视觉模型 (dsh 同款准入语义) ——
  // 前端只做体验级预检 (模态门/条数); 字节/像素/单边限额的权威校验在 C++ 附件仓,
  // 拒绝原因原样弹在对话流里。
  function addPendingImages(files) {
    const images = Array.from(files || []).filter(
      (f) => f && f.type && f.type.indexOf('image/') === 0);
    if (!images.length) return;
    const extOf = (type) => ({ 'image/png': 'png', 'image/jpeg': 'jpg',
      'image/webp': 'webp', 'image/gif': 'gif' }[type] || 'img');
    for (const file of images) {
      if (state.pendingImages.length >= 20) {
        log('warn', ['单条消息最多 20 张图, 其余已忽略']);
        break;
      }
      state.pendingImages.push({
        file,
        previewUrl: URL.createObjectURL(file),
        name: 'paste-' + Date.now() + '-' + state.pendingImages.length
              + '.' + extOf(file.type),
      });
    }
    renderPendingThumbs();
  }

  function renderPendingThumbs() {
    const bar = dom.pasteThumbs;
    bar.innerHTML = '';
    bar.hidden = state.pendingImages.length === 0;
    state.pendingImages.forEach((item, idx) => {
      const box = document.createElement('div');
      box.className = 'paste-thumb';
      box.title = item.name;
      const img = document.createElement('img');
      img.src = item.previewUrl;
      const rm = document.createElement('button');
      rm.className = 'rm';
      rm.textContent = '×';
      rm.addEventListener('click', () => removePendingImage(idx));
      box.appendChild(img);
      box.appendChild(rm);
      bar.appendChild(box);
    });
  }

  function removePendingImage(idx) {
    const item = state.pendingImages[idx];
    if (!item) return;
    URL.revokeObjectURL(item.previewUrl);
    state.pendingImages.splice(idx, 1);
    renderPendingThumbs();
  }

  function clearPendingImages() {
    for (const item of state.pendingImages) URL.revokeObjectURL(item.previewUrl);
    state.pendingImages.length = 0;
    renderPendingThumbs();
  }

  function readImageAsEntry(file) {
    return new Promise((resolve) => {
      const reader = new FileReader();
      reader.onload = () => {
        const dataUrl = String(reader.result || '');
        const comma = dataUrl.indexOf(',');
        resolve(comma >= 0 ? { data: dataUrl.slice(comma + 1),
                              mediaType: file.type } : null);
      };
      reader.onerror = () => resolve(null);
      reader.readAsDataURL(file);
    });
  }

  dom.inputBox.addEventListener('paste', (e) => {
    const files = [];
    for (const item of (e.clipboardData ? e.clipboardData.items : [])) {
      if (item.kind === 'file') {
        const f = item.getAsFile();
        if (f && f.type && f.type.indexOf('image/') === 0) files.push(f);
      }
    }
    if (files.length) {
      e.preventDefault();
      addPendingImages(files);
    }
  });
  // 拖拽图片进窗口同入口 (文本拖拽不受影响)
  window.addEventListener('dragover', (e) => e.preventDefault());
  window.addEventListener('drop', (e) => {
    e.preventDefault();
    if (e.dataTransfer && e.dataTransfer.files) addPendingImages(e.dataTransfer.files);
  });

  async function sendVia(mode) {
    if (!state.session) return;
    const text = dom.inputBox.value;
    const hasImages = mode === 'followup' && state.pendingImages.length > 0;
    if (!text && !hasImages) return;

    if (mode === 'followup' && hasImages) {
      // 模态门 (dsh MODEL_DOES_NOT_SUPPORT_IMAGES 的前端预检): 纯文本主模型上
      // 发图只会让端点 400, 不如直接拦下。
      if (state.selected && state.selected.imageInput === false) {
        renderers.appendMessage('system', null,
          '● 当前模型不支持图像输入 — 换 VISION 模型或点缩略图上的 × 移除贴图');
        return;
      }
      const entries = [];
      for (const item of state.pendingImages) {
        const entry = await readImageAsEntry(item.file);
        if (!entry) {
          renderers.appendMessage('system', null, '● 图片读取失败, 请重试');
          return;
        }
        entry.name = item.name;
        entries.push(entry);
      }
      const r = state.session.followupImages(text || '[图片]', entries);
      if (!r.ok) {
        renderers.appendMessage('error', null, '[发送失败] ' + (r.error || '未知原因'));
        log('error', ['followupImages:', r.error]);
        return;
      }
      const userEl = renderers.appendMessage('user', null, text || '[图片]');
      for (const url of previewUrlsOf(state.pendingImages)) {
        const img = document.createElement('img');
        img.className = 'msg-image';
        img.src = url;
        userEl.appendChild(img);
      }
      clearPendingImages();
      dom.inputBox.value = '';
      setStatusFlag(true);
      return;
    }

    if (!text) return;
    if (mode === 'followup') {
      state.session.followup(text);
      renderers.appendMessage('user', null, text);
      log('in', ['followup: ' + text.slice(0, 80)]);
    } else if (mode === 'steer') {
      state.session.steer(text);
      renderers.appendMessage('user', null, text);
      log('in', ['steer: ' + text.slice(0, 80)]);
    } else if (mode === 'inject') {
      state.session.inject(text);
      renderers.appendMessage('system', null, '[inject] ' + text);
      log('in', ['inject: ' + text.slice(0, 80)]);
    }
    dom.inputBox.value = '';
    setStatusFlag(true);
  }

  // 贴图的本地预览 URL 列表 (用户气泡内嵌小图用)
  function previewUrlsOf(items) { return items.map((it) => it.previewUrl); }

  dom.btnSend.addEventListener('click', () => sendVia('followup'));
  dom.btnSteer.addEventListener('click', () => sendVia('steer'));
  dom.btnInject.addEventListener('click', () => sendVia('inject'));

  function cancelRunning() {
    if (!state.session) return;
    if (state.session.status() !== 1) return;
    state.session.cancel(A.CancelCause.USER);
    log('warn', ['cancel(user) 已发出']);
    renderers.appendMessage('system', null, '● 已请求取消');
    // 防御性收尾: 不等 C++ 的 turn/end (race / silent abort 时不会到)。
    // 当前 chunk 有文本 → 改成 .error + 加 [cancelled] meta; 空 → 直接移除避免占位卡。
    if (state.currentAssistantEl) {
      state.currentAssistantEl.classList.remove('running');
      if (state.currentAssistantText.trim()) {
        state.currentAssistantEl.classList.add('error');
        const meta = document.createElement('span');
        meta.className = 'meta';
        meta.textContent = '[cancelled]';
        state.currentAssistantEl.insertBefore(meta, state.currentAssistantEl.firstChild);
      } else {
        state.currentAssistantEl.remove();
      }
      state.currentAssistantEl = null;
      state.currentAssistantText = '';
    }
    state.cancelInFlight = true; // 告诉 handleTurnEnd: cancel 已手动收尾, 别再重复建卡
    syncRunStopButtons();
  }
  dom.btnStop.addEventListener('click', cancelRunning);

  dom.inputBox.addEventListener('keydown', (e) => {
    if (e.key === 'Enter' && !e.shiftKey) {
      e.preventDefault();
      if (e.altKey) sendVia('steer');
      else if (e.ctrlKey) sendVia('inject');
      else sendVia('followup');
    } else if (e.key === 'Escape') {
      // Esc 取消在跑的任务 (焦点在输入框时); 输入框为空且 session 在跑才生效.
      if (state.session && state.session.status() === 1) {
        e.preventDefault();
        cancelRunning();
      }
    }
  });
  // 全局 Esc 也行 — 焦点不在输入框时也能触发
  document.addEventListener('keydown', (e) => {
    if (e.key === 'Escape' && state.session && state.session.status() === 1) {
      // 避免在输入框里 Esc 与浏览器自带的 blur/clear 冲突, 上面 inputBox 已处理;
      // 这里的目的是焦点在按钮/对话区时也能 Esc 取消.
      if (document.activeElement !== dom.inputBox) {
        e.preventDefault();
        cancelRunning();
      }
    } else if (e.key === 'Escape' && dom.modelsEditor.classList.contains('shown')) {
      closeEditor();
    }
  });

  // textarea 自动高度
  dom.inputBox.addEventListener('input', () => {
    dom.inputBox.style.height = 'auto';
    dom.inputBox.style.height = Math.min(200, dom.inputBox.scrollHeight) + 'px';
  });

  // ----- Skill 已从 UI 移除 (顶层 A.runSkill API 仍可用, 通过 DevTools 或其它脚本调) -----

  // ----- 事件日志 -----
  dom.btnClearEvents.addEventListener('click', () => { dom.eventLog.innerHTML = ''; });
  dom.btnClearMessages.addEventListener('click', clearMessages);

  dom.btnReplay.addEventListener('click', () => {
    if (!state.session) return;
    const events = state.session.snapshotRecent(50);
    log('in', ['快照回放最近 ' + events.length + ' 条']);
    clearMessages();
    for (const ev of events) {
      const t = ev.type || (ev.raw ? 'raw' : 'unknown');
      const d = ev.data || {};
      const summary = summarizeEvent(t, d);
      if (summary) renderers.appendMessage('system', `[${ev.seq}] ${t}`, summary);
    }
  });

  function summarizeEvent(type, data) {
    try {
      switch (type) {
        case 'assistant/chunk':
          if (data.chunk && data.chunk.text != null) return '[delta] ' + data.chunk.text;
          if (data.chunk && data.chunk.thinking != null) return '[reasoning] ' + data.chunk.thinking;
          return '';
        case 'assistant/message': return '[message]';
        case 'tool/call': return '[call] ' + (data.name || '') + ' ' + (data.callId || '');
        case 'tool/result': return '[result] ' + (data.callId || '');
        case 'turn/start': return '[start]';
        case 'turn/end': return '[end] ' + JSON.stringify(data.reason || {});
        case 'session/open':
        case 'session/close': return JSON.stringify(data);
        default: return JSON.stringify(data).slice(0, 200);
      }
    } catch (_) { return ''; }
  }

  // ----- observer 回调 -----
  // 工具专属 renderer 注册表由 chat_render.js 提供 (renderers.TOOL_RENDERERS)。
  // 加新工具 = 改 chat_render.js, 控制流不动。

  // 关闭当前 assistant chunk: 流式暂停 (切到 tool 调用 / 收尾)。
  // 文本非空就丢 "running" 状态 (橙边 → 绿边); 文本空就整块移除 (避免空卡占位)。
  // 下次 token 来时因 state.currentAssistantEl=null 自动建新 chunk, 跟 tool 卡按时间顺序交错.
  function closeAssistantChunk() {
    if (!state.currentAssistantEl) return;
    if (!state.currentAssistantText.trim()) {
      state.currentAssistantEl.remove();
    } else {
      state.currentAssistantEl.classList.remove('running');
    }
    state.currentAssistantEl = null;
    state.currentAssistantText = '';
  }

  function handleToken(text) {
    if (text == null) return;
    // 跨 token 切 <think>...</think> — buffer 累积, 见到完整块就跳过 (避免跟 handleReasoning 重复).
    state.tokenBuffer += text;
    let out = '';
    while (state.tokenBuffer.length > 0) {
      if (state.inThink) {
        const endIdx = state.tokenBuffer.indexOf('</think>');
        if (endIdx < 0) { state.tokenBuffer = ''; break; }
        state.tokenBuffer = state.tokenBuffer.slice(endIdx + '</think>'.length);
        state.inThink = false;
      } else {
        const startIdx = state.tokenBuffer.indexOf('<think>');
        if (startIdx < 0) { out += state.tokenBuffer; state.tokenBuffer = ''; break; }
        out += state.tokenBuffer.slice(0, startIdx);
        state.tokenBuffer = state.tokenBuffer.slice(startIdx + '<think>'.length);
        state.inThink = true;
      }
    }
    if (!out) return;
    if (!state.currentAssistantEl) {
      state.currentAssistantEl = renderers.appendMessage('assistant running', null, '');
      state.currentAssistantText = '';
    }
    // 更新前先看用户在不在底, 不在底就别 auto-scroll, 让用户继续看前面内容.
    const stick = isAtBottom();
    state.currentAssistantText += out;
    const body = state.currentAssistantEl.querySelector('.body');
    if (body) body.innerHTML = renderers.markdown(state.currentAssistantText);
    if (stick) dom.messages.scrollTop = dom.messages.scrollHeight;
  }

  function handleReasoning(text) {
    if (text == null) return;
    if (!state.currentReasoningEl) {
      state.currentReasoningEl = renderers.appendMessage('reasoning', '[思考]', '');
      state.currentReasoningText = '';
    }
    // 同上: 滚离了就不跟.
    const stick = isAtBottom();
    state.currentReasoningText += text;
    // 折叠卡流式刷新: body markdown + summary 摘要/字数
    renderers.updateReasoning(state.currentReasoningEl, state.currentReasoningText);
    if (stick) dom.messages.scrollTop = dom.messages.scrollHeight;
  }

  // step/end 不再需要 move: assistant chunk 已在自然位置 (tool 卡穿插)
  function handleSessionEvent(seq, typeName, evJson) {
    const level = typeName.startsWith('error') || typeName.endsWith('/error') ? 'error'
      : typeName.startsWith('tool') ? 'tool' : '';
    if (level) log(level, ['[' + seq + ']', typeName]);
  }

  function handleToolCall(name, argsJson, callId) {
    if (!name) return;
    // 先关闭上一个 assistant chunk, 让 tool_call 按时间顺序出现在 chunk 之后
    closeAssistantChunk();
    // 工具专属 renderer 注册表: 命中走专属渲染, 未命中走通用 tool-call 卡。
    // 新工具加专属渲染 = 改 chat_render.js 的 TOOL_RENDERERS, 不动 handleToolCall。
    const renderer = renderers.TOOL_RENDERERS[name];
    if (renderer) {
      renderer(argsJson || '{}', callId);
    } else {
      let argsPretty = argsJson || '';
      try { argsPretty = JSON.stringify(JSON.parse(argsJson), null, 2); } catch (_) {}
      renderers.appendMessage('tool-call', (callId ? '[' + callId + '] ' : '') + name, argsPretty);
    }
    if (name && callId) maybeAskBeforeCall(name, callId, argsJson);
  }

  function handleToolResult(name, resultText, ok, callId) {
    // tool_call 时已经关过一次 chunk; tool_result 通常没新 chunk, 防御性再关一次
    closeAssistantChunk();
    const cls = 'tool-result' + (ok ? '' : ' error');
    let body = resultText || '';
    if (body.length > 4000) body = body.slice(0, 4000) + '\n…(截断)';
    const meta = '[result]' + (callId ? ' ' + callId : '') + (name ? ' ' + name : '') + (ok ? '' : ' (失败)');
    renderers.appendMessage(cls, meta, body);
  }

  function handleTurnEnd(content, error) {
    // cancel 已在 cancelRunning 里手动收尾, 这里只清理状态不复建卡 / 不重复加 meta
    if (state.cancelInFlight) {
      state.cancelInFlight = false;
      state.currentAssistantEl = null;
      state.currentAssistantText = '';
      state.currentReasoningEl = null;
      state.currentReasoningText = '';
      state.tokenBuffer = '';
      state.inThink = false;
      setStatusFlag(false);
      return;
    }
    if (state.currentAssistantEl && state.currentAssistantText) {
      state.currentAssistantEl.className = 'msg assistant' + (error ? ' error' : '');
      const meta = document.createElement('span');
      meta.className = 'meta';
      meta.textContent = error ? '[turn end · failed]' : '[turn end]';
      state.currentAssistantEl.insertBefore(meta, state.currentAssistantEl.firstChild);
    } else if (content) {
      // 没走 handleToken (全部内容都是 think 块时), 这里也剥掉 <think>...</think>, 避免重复.
      const stripped = String(content).replace(/<think>[\s\S]*?<\/think>/g, '');
      if (stripped.trim()) {
        renderers.appendMessage('assistant' + (error ? ' error' : ''),
                      error ? '[turn end · failed]' : '[turn end]', stripped);
      }
    } else if (error) {
      renderers.appendMessage('assistant error', '[turn end · failed]', error);
    }
    if (error) log('error', ['turn end 错误:', error]);
    else log('success', ['turn end', (content || '').length + ' 字符']);
    state.currentAssistantEl = null;
    state.currentAssistantText = '';
    state.currentReasoningEl = null;
    state.currentReasoningText = '';
    state.tokenBuffer = '';
    state.inThink = false;
    setStatusFlag(false);
  }

  function handleStatus(status) {
    setStatusFlag(status === 1);
  }

  // ----- 审批 (异步两阶段) -----
  function askApprovalLog(toolName, callId, reason) {
    log('warn', ['[ask] tool=' + toolName + ' callId=' + callId + ' reason=' + (reason || '')]);
  }

  function maybeAskBeforeCall(toolName, callId, argsJson) {
    showApprovalDialog(toolName, callId, 'agent 请求调用 ' + toolName);
  }

  function showApprovalDialog(toolName, callId, reason) {
    if (state.pendingApprovalResolve) {
      try { state.pendingApprovalResolve(A.ApprovalAnswer.REJECT); } catch (_) {}
      hideApprovalDialog();
    }
    dom.approvalTool.textContent = toolName || '';
    dom.approvalCallId.textContent = callId || '';
    dom.approvalReason.textContent = reason || '';
    dom.approvalDialog.classList.add('shown');
    state.pendingApprovalReq = { toolName, callId, reason };
    return new Promise((resolve) => {
      state.pendingApprovalResolve = (answer) => {
        if (state.host) state.host.stageApprovalAnswer(answer);
        resolve(answer);
      };
    });
  }

  function hideApprovalDialog() {
    dom.approvalDialog.classList.remove('shown');
    state.pendingApprovalReq = null;
    state.pendingApprovalResolve = null;
  }

  function approvalButton(answer, label) {
    if (state.pendingApprovalResolve) {
      const resolve = state.pendingApprovalResolve;
      hideApprovalDialog();
      resolve(answer);
      log('in', ['审批', label, '(staged)']);
    }
  }
  dom.btnAllow.addEventListener('click', () => approvalButton(A.ApprovalAnswer.ALLOW_ONCE, '允许'));
  dom.btnReject.addEventListener('click', () => approvalButton(A.ApprovalAnswer.REJECT, '拒绝'));
  dom.btnACancel.addEventListener('click', () => approvalButton(A.ApprovalAnswer.CANCEL, '取消'));

  // ----- 启动 -----
  function boot() {
    const nativeReady = !!(window.avox && typeof window.avox.createAgentHost === 'function');
    if (!nativeReady) {
      const msg = '原生 avox_agent 未加载 — 检查 preload.js 与 avox_js.node 是否链接 AgentExport';
      showError(msg);
      setHostStatus('未就绪', 'error');
      renderers.appendMessage('system', null, msg);
      return;
    }
    setHostStatus('加载中…', '');
    refreshProviderList();
    if (state.selected) autoStart();

    // 主进程诊断环 — 仅在 settings 面板打开时输出, 否则静默.
    setInterval(() => {
      if (!state.settingsOpen) {
        if (window.avox && typeof window.avox.drainAgentDiag === 'function') {
          window.avox.drainAgentDiag();
        }
        return;
      }
      if (window.avox && typeof window.avox.drainAgentDiag === 'function') {
        const lines = window.avox.drainAgentDiag();
        for (const l of lines) log('in', ['[diag]', l]);
      }
    }, 1000);
  }

  document.addEventListener('DOMContentLoaded', boot);
  if (document.readyState !== 'loading') boot();
})();
