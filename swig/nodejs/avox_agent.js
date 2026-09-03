// avox_agent 的 JS 包装 — 与 swig/common.i 的 SWIG 包装层对接。
//
// **重要: 跨 contextBridge 边界的限制**
// SWIG napi 把 host/session 包装成 Napi::ObjectWrap 实例, 方法挂在 prototype 上。
// contextBridge 在 contextIsolation=true 下**只深克隆 own properties, 不复制 prototype**
// — 渲染进程拿到的 host/session 调方法会报 "is not a function"。
//
// **架构**: host/session 状态全留在主进程 (preload.js), 渲染进程走离散 IPC:
//   - hostOpenAgent / hostCloseAgent / hostShutdown / hostAddPromptSection /
//     hostSetApprovalUi / hostLastError / hasHost / hasSession
//   - sessionFollowup / sessionSteer / sessionInject / sessionCancel /
//     sessionWaitIdle / sessionStatus / sessionCompactNow / sessionAddObserver /
//     sessionRemoveObserver / sessionEventCount / sessionEventJson / sessionTrackPath /
//     sessionSnapshotRecent
//   - stageApprovalAnswer — 渲染进程预置下一次 ask 的答案
//   - runSkill / listProviders — 顶层独立调用
//
// observer 与 approval askHandler 仍是 JS 对象 (renderer 那边的方法), 经 contextBridge
// 自动 proxy; 主进程侧 ISessionObserver / IApprovalUi 包装持有 proxy, 触发回调时
// IPC 转发回 renderer 调真实方法。

(function (root) {
  'use strict';

  // ----------------------------------------------------------------
  //  SessionObserver: 把回调收成单一对象, 喂给 session.addObserver IPC
  //
  //  driver 线程吐事件是非阻塞的 (NonBlockingCall), 所以**不要在回调里同步等其它回调**
  //  (会丢事件)。合理用法:
  //    - 只更新 DOM / 推入本地队列;
  //    - 重活 (HTTP / 大计算) 另起 setTimeout。
  // ----------------------------------------------------------------
  class SessionObserver {
    constructor(handlers = {}) {
      this.onSessionEvent = handlers.onSessionEvent || null;
      this.onToken        = handlers.onToken        || null;
      this.onReasoning    = handlers.onReasoning    || null;
      this.onToolCall     = handlers.onToolCall     || null;
      this.onToolResult   = handlers.onToolResult   || null;
      this.onTurnEnd      = handlers.onTurnEnd      || null;
      this.onStatus       = handlers.onStatus       || null;
    }
  }

  // ----------------------------------------------------------------
  //  ApprovalUi: 包装一个 ask 函数, 传给 hostSetApprovalUi IPC.
  //  真实**同步阻塞**语义由主进程侧 stage 答案支撑, 见 preload.js / ask().
  // ----------------------------------------------------------------
  class ApprovalUi {
    constructor(askHandler) {
      this.ask = typeof askHandler === 'function' ? askHandler : null;
    }
  }

  // ----------------------------------------------------------------
  //  AgentSession: 状态全在主进程, 这边全是转发
  // ----------------------------------------------------------------
  class AgentSession {
    constructor(ownerHost, trackPath) {
      this._host = ownerHost;
      this._trackPath = trackPath || '';
      this._observerHandles = [];     // 主进程返回的 handle, 用于 removeObserver
    }

    followup(text) { root.avox.sessionFollowup(text == null ? '' : String(text)); }
    // 带图 followup: images = [{base64, mediaType, name?}]; 返回 {ok, error}。
    followupImages(text, images) {
      return root.avox.sessionFollowupImages(
        text == null ? '' : String(text),
        Array.isArray(images) ? images : []);
    }
    steer(text)    { root.avox.sessionSteer(text == null ? '' : String(text)); }
    inject(text)   { root.avox.sessionInject(text == null ? '' : String(text)); }
    cancel(cause)  { root.avox.sessionCancel(cause | 0); }
    waitIdle(timeoutMs) { return !!root.avox.sessionWaitIdle(timeoutMs | 0); }
    status()       { return root.avox.sessionStatus() | 0; }
    compactNow()   { return !!root.avox.sessionCompactNow(); }

    addObserver(observer) {
      if (!(observer instanceof SessionObserver)) {
        throw new TypeError('addObserver expects a SessionObserver');
      }
      const handlers = {
        onSessionEvent: observer.onSessionEvent,
        onToken:        observer.onToken,
        onReasoning:    observer.onReasoning,
        onToolCall:     observer.onToolCall,
        onToolResult:   observer.onToolResult,
        onTurnEnd:      observer.onTurnEnd,
        onStatus:       observer.onStatus,
      };
      const r = root.avox.sessionAddObserver(handlers);
      if (!r || !r.ok) {
        throw new Error('sessionAddObserver failed: ' + (r && r.error));
      }
      this._observerHandles.push(r.handle);
      return () => this.removeObserverByHandle(r.handle);
    }

    removeObserver(observer) {
      // 现在没法反向索引; 直接全部摘掉
      for (const h of this._observerHandles) {
        try { root.avox.sessionRemoveObserver(h); } catch (_) {}
      }
      this._observerHandles = [];
    }

    removeObserverByHandle(handle) {
      try { root.avox.sessionRemoveObserver(handle); } catch (_) {}
      const i = this._observerHandles.indexOf(handle);
      if (i >= 0) this._observerHandles.splice(i, 1);
    }

    eventCount() { return root.avox.sessionEventCount() | 0; }
    eventJson(seq) {
      try { return root.avox.sessionEventJson(seq | 0); } catch (_) { return null; }
    }
    trackPath() { return this._trackPath || root.avox.sessionTrackPath() || ''; }

    snapshotRecent(n = 50) {
      return root.avox.sessionSnapshotRecent(n | 0);
    }
  }

  // ----------------------------------------------------------------
  //  AgentHost: 状态全在主进程 (preload.js 持有 nativeHost); 这边只暴露接口
  // ----------------------------------------------------------------
  class AgentHost {
    constructor() {
      this._session = null;
      this._closed = false;
      this._approvalUi = null;
      this._promptSections = [];
    }

    addPromptSection(name, order, text) {
      if (this._closed) return false;
      const r = root.avox.hostAddPromptSection(name, order, text);
      if (r && r.ok) {
        this._promptSections.push({ name, order, text });
        return true;
      }
      return false;
    }

    setApprovalUi(ui) {
      if (this._closed) return;
      if (ui == null) {
        this._approvalUi = null;
        return;
      }
      if (!(ui instanceof ApprovalUi)) {
        throw new TypeError('setApprovalUi expects an ApprovalUi');
      }
      this._approvalUi = ui;
      // askHandler 直接当 callback 传给主进程, 主进程在 ask() 时调一次 (仅日志钩子,
      // 真同步答案靠 stageApprovalAnswer). 把 askHandler 挂到一个全局, 让 page 在 ask
      // 之前调 stageApprovalAnswer 把答案预置进主进程 pendingApprovalAnswer 槽.
      root.avox.hostSetApprovalUi(ui.ask);
    }

    // 渲染进程预置下一次 ask 的答案 (主进程 ask() 时立刻取走).
    stageApprovalAnswer(answer) {
      root.avox.stageApprovalAnswer(answer | 0);
    }

    openAgent(sessionId) {
      if (this._closed) throw new Error('host already shut down');
      if (this._session) throw new Error('a session is already open; call closeAgent() first');
      const r = root.avox.hostOpenAgent(sessionId || '');
      if (!r || !r.ok) {
        throw new Error('openAgent failed: ' + (r && r.error || 'unknown'));
      }
      this._session = new AgentSession(this, r.trackPath || '');
      return this._session;
    }

    closeAgent() {
      if (!this._session) return;
      try { root.avox.hostCloseAgent(); } catch (_) {}
      this._session = null;
    }

    get currentSession() { return this._session; }

    shutdown() {
      if (this._closed) return;
      this._closed = true;
      this._session = null;
      try { root.avox.hostShutdown(); } catch (_) {}
    }

    get closed() { return this._closed; }
    lastError() { return root.avox.hostLastError() || ''; }
  }

  // ----------------------------------------------------------------
  //  工厂: 在 window.avox 上挂接的快捷入口
  // ----------------------------------------------------------------
  function createAgentHost(configJson) {
    if (!root.avox || typeof root.avox.createAgentHost !== 'function') {
      return { host: null, error: 'native binding not loaded' };
    }
    const r = root.avox.createAgentHost(configJson || {});
    if (!r || !r.ok) {
      return { host: null, error: (r && r.error) || 'createAgentHost failed' };
    }
    return { host: new AgentHost(), error: null };
  }

  function runSkill(name, input) {
    if (!root.avox || typeof root.avox.runSkill !== 'function') {
      return { skill: name, error: 'native binding not loaded' };
    }
    try {
      const raw = root.avox.runSkill(name || '', input == null ? '' : String(input));
      try {
        const obj = JSON.parse(raw);
        if (obj && typeof obj === 'object') return obj;
      } catch (_) {}
      return { skill: name, output: raw };
    } catch (e) {
      return { skill: name, error: String(e && e.message || e) };
    }
  }

  function listProviders() {
    if (!root.avox || typeof root.avox.listProviders !== 'function') {
      return { entries: [], error: 'native binding not loaded' };
    }
    let parsed;
    try { parsed = JSON.parse(root.avox.listProviders()); }
    catch (e) { return { entries: [], error: 'parse failed: ' + e.message }; }
    if (!Array.isArray(parsed)) return { entries: [], error: 'unexpected shape' };
    return { entries: parsed, error: null };
  }

  const CancelCause = Object.freeze({ USER: 0, PARENT: 1, HOOK: 2, DISPOSED: 3 });
  const ApprovalAnswer = Object.freeze({
    ALLOW_ONCE: 0, REJECT: 1, CANCEL: 2, UNAVAILABLE: 3,
  });

  const api = {
    AgentHost, AgentSession, SessionObserver, ApprovalUi,
    createAgentHost, runSkill, listProviders,
    CancelCause, ApprovalAnswer,
  };

  if (typeof module !== 'undefined' && module.exports) {
    module.exports = api;
  } else {
    root.AvoxAgent = api;
  }
})(typeof window !== 'undefined' ? window : globalThis);