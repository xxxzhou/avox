const { contextBridge, ipcRenderer } = require('electron');
const fs = require('fs');
const path = require('path');
const os = require('os');

// 加载 C++ 模块
const avox = require('../../build/windows/avox/install/AMD64/Release/avox_js.node');
const { YuvWebGPURender, YuvGLRender, kMaxWebglContexts } = require('./yuvglrender.js');

// ============================================================================
// providers.json 编辑支持 (主进程直接读写; C++ AssetLoader 已优先读 user-level)
// ----------------------------------------------------------------------------
// user-level: %LOCALAPPDATA%/avox/config/providers.json (写)
// assets:    <addon dir>/assets/config/providers.json (只读, 初始默认)
// ============================================================================

function userConfigPath(name) {
  const localAppData = process.env.LOCALAPPDATA
    || path.join(os.homedir(), 'AppData', 'Local');
  const dir = path.join(localAppData, 'avox', 'config');
  if (!fs.existsSync(dir)) fs.mkdirSync(dir, { recursive: true });
  return path.join(dir, name);
}

function addonDir() {
  try {
    return path.dirname(require.resolve(
      '../../build/windows/avox/install/AMD64/Release/avox_js.node'));
  } catch (_) { return ''; }
}

function readProvidersJsonAny() {
  const up = userConfigPath('providers.json');
  if (fs.existsSync(up)) {
    return { ok: true, json: fs.readFileSync(up, 'utf8'), path: up, source: 'user' };
  }
  const ap = path.join(addonDir(), 'assets', 'config', 'providers.json');
  if (fs.existsSync(ap)) {
    return { ok: true, json: fs.readFileSync(ap, 'utf8'), path: ap, source: 'assets' };
  }
  return { ok: false, error: 'providers.json 不存在 (user-level 与 assets 都查不到)' };
}

// ============================================================================
// avox_agent 适配层 (主进程持有 host/session, 渲染进程走离散 IPC)
// ----------------------------------------------------------------------------
// **设计**: SWIG 包装的 host/session 是 napi ObjectWrap 实例, 其方法挂在 prototype
// 上. contextBridge 跨 contextIsolation=true 边界时**只深克隆 own properties, 不
// 复制 prototype 方法** —— 渲染进程拿到的是方法全空的伪对象, host.openAgent() 就
// 报 "is not a function". 解决: 全部生命周期与状态留在主进程, 渲染进程只调离散
// IPC (hostOpenAgent / sessionFollowup 等), 不接触 native 句柄.
//
// 观察者与审批应答仍是 JS 对象 (函数), 经 contextBridge 自动 proxy, C++ 侧
// queueCallback 触发时 IPC 回渲染进程调真实回调.
// ============================================================================

let nativeHost = null;          // IAgentHost* (主进程)
let nativeSession = null;       // IAgentSession* (主进程, host 拥有)
let observerHandleCounter = 0;
const observerHandles = new Map();   // handle -> native ISessionObserver*
const promptSections = [];           // [{name, order, text}]
let lastErrorCache = '';             // host.lastError() 缓存 (避免重复 IPC)

// 主进程侧 observer 包装 — methods 直接调 renderer 的回调. contextBridge 把渲染
// 进程的对象传过来时是 proxy, callback 走 IPC 转发.
//
// **关键**: C++ driver 经 JsObserver.hpp TSFN 走 `obj.Has(name).Get(name).Call(args)`;
// `napi_has_named_property` 只查 own property, **不走原型链** — 方法必须在构造时
// 挂到 this (own property), 不能挂 prototype. 这是与 avox_media 等其他 observer 不
// 同的地方 (其他 observer 是用 JS 派生类实现虚函数, SWIG director 派发, 不走 JsObserver.hpp).
class JsSessionObserver {
  constructor(handlers) {
    const pick = (key) => {
      const v = handlers ? handlers[key] : null;
      const t = typeof v;
      agentDiag.push(`pick(${key}) typeof=${t}`);
      return t === 'function' ? v : null;
    };
    const wrap = (key) => {
      const cb = pick(key);
      // own-property 包装: 让 C++ napi_has_named_property 找得到; 内部再转发给 renderer proxy.
      // 用箭头函数保留 this (即 JsSessionObserver 实例), 但也避免重复 pick 导致 typeof 重判.
      return (...a) => {
        agentDiag.push(`call:${key} n=${a.length}`);
        if (cb) try { cb(...a); } catch (e) { agentDiag.push(`err ${key} ${e.message}`); }
      };
    };
    this.onSessionEvent = wrap('onSessionEvent');
    this.onToken        = wrap('onToken');
    this.onReasoning    = wrap('onReasoning');
    this.onToolCall     = wrap('onToolCall');
    this.onToolResult   = wrap('onToolResult');
    this.onTurnEnd      = wrap('onTurnEnd');
    this.onStatus       = wrap('onStatus');
  }
}

// 主进程诊断环; 通过 contextBridge 暴露, renderer 轮询拉走贴到 eventLog.
const agentDiag = [];

// 审批: ask 是同步阻塞 driver 线程. contextBridge 跨进程调用是异步, 不能真同步, 故
// 这里保留一个最近答案的 slot. 渲染进程**先**调 avox.stageApprovalAnswer(answer)
// 预置答案, ask 时立刻取走. 不预置时按 UNAVAILABLE 兜底 (fail-closed).
class JsApprovalUi {
  constructor(askHandler) {
    // askHandler 是 renderer 的函数, 渲染进程可异步调用它收集用户选择然后 stage.
    // 但 driver 线程要同步答案, 单靠 stage 不能覆盖同步 ask 的语义; 仍以 pre-staged
    // 兜底为主, askHandler 留作日志钩子.
    this.askHandler = typeof askHandler === 'function' ? askHandler : null;
  }
  // 渲染进程调 avox.stageApprovalAnswer 塞答案; ask 直接读.
  ask(toolName, callId, reason) {
    const a = pendingApprovalAnswer;
    pendingApprovalAnswer = -1;  // 一次性消费
    if (this.askHandler) {
      try { this.askHandler(toolName, callId, reason); } catch (_) {}
    }
    if (a === 0) return 0;  // ALLOW_ONCE
    if (a === 1) return 1;  // REJECT
    if (a === 2) return 2;  // CANCEL
    return 3;               // UNAVAILABLE
  }
}
let pendingApprovalAnswer = -1;
let approvalUiInstalled = false;

class JsLogOb {
  constructor() {
    this.logFn = null;
  }
  onLogEvent(level, msg) {
    if (this.logFn) this.logFn(level, msg);
  }
}

class JsMediaPlayerOb {
  // player可能是mediaplayer/sourceplayer
  constructor(player) {
    this.player = player;
  }
  // IMediaPlayerOb 接口实现
  onStateChange(preState, state) {
    console.log("preload onStateChange:", preState, state);
    const preStateStr = avox.getPlayerStateStr(preState);
    const stateStr = avox.getPlayerStateStr(state);
    // 直接触发 JS 事件，避免双重处理
    this.player.handleStateChange(preStateStr, stateStr);
  }
  onIoError(error, msg) {
    this.player.handleIoError(error, msg);
  }
  onDecodeError(trackType, error) {
    this.player.handleDecodeError(trackType, error);
  }
  onReady() {
    this.player.handleReady();
  }
  onComplete() {
    this.player.handleComplete();
  }
  onSeek() {
    this.player.handleSeek();
  }
  onPause() {
    this.player.handlePause();
  }
  onResume() {
    this.player.handleResume();
  }
  onClose() {
    this.player.handleClose();
  }
}

class JsSurfaceRenderOb {
  constructor(player) {
    this.player = player;
    // 这个值C++会回读,如要改名,请JsSurfaceRenderOb.cpp也要一致
    // 为true表示JS申请的内存,会把JS内存给C++,C++负责更新
    this.jsManagedBuffer = true;
    // autoSizeScale: 自动根据canvas/video大小计算enableSizeScale
    this.autoSizeScale = true;
    // autoAspect: 自适应长宽比,默认true保持原始比例(黑边),false拉伸填满
    this.autoAspect = true;
    // 上一帧视频尺寸,用于检测变化通知渲染器
    this.lastFrameW = 0;
    this.lastFrameH = 0;
    // 当前auto计算的scale档位,用于跨档时才触发(天然防抖)
    this.currentAutoScale = 1.0;
    // offscreen未就绪时暂存resize参数
    this.pendingResize = null;
  }
  setOffscreen(offscreen) {
    this.offscreen = offscreen;
    console.log("setOffscreen:", this.offscreen);
    try {
      // 防御: 若该路之前已挂过渲染器(如换流时重建), 先释放旧的,
      // 否则 GL 计数只增不减(配额虚高) + GPU 资源泄漏
      if (this.renderer && typeof this.renderer.destroy === 'function') {
        try { this.renderer.destroy(); } catch (e0) { console.error('old renderer destroy error:', e0); }
        this.renderer = null;
      }
      // WebGL2 活跃上下文有浏览器硬上限(约16个). 以前用 playerInstances.size 判断,
      // 数的是"创建过的播放器总数", 布局切换/异步销毁后虚高, 路数一多全挤到 WebGPU
      // 又各自 requestDevice, 超预算被整批回收 -> Invalid RenderPipeline + 全黑.
      // 现在: 按"当前存活的 GL 渲染器数"配额, 配额内走久经考验的 WebGL2;
      // 超配额走共享单一 device 的 WebGPU (多少路都只占一个 device).
      if (navigator.gpu && YuvGLRender.aliveContexts() < kMaxWebglContexts) {
        this.renderer = new YuvGLRender(this.offscreen);
      } else {
        this.renderer = new YuvWebGPURender(this.offscreen);
      }
    } catch (e) {
      console.error("渲染器初始化失败:", e);
    }
    // 同步autoAspect状态到渲染器
    if (this.renderer && typeof this.renderer.setAutoAspect === 'function') {
      this.renderer.setAutoAspect(this.autoAspect);
    }
    // 初始化完成后立即清屏为黑色，避免显示随机内容
    this.clearScreen();
    // 应用pending resize(RESIZE_CANVAS可能在setOffscreen之前到达)
    if (this.pendingResize) {
      this.resize(this.pendingResize.width, this.pendingResize.height);
      this.pendingResize = null;
    }
  }
  clearScreen() {
    if (this.renderer && typeof this.renderer.clear === 'function') {
      this.renderer.clear();
    }
    // 清除待渲染帧
    this.pendingFrame = null;
  }
  resize(width, height) {
    if (!this.offscreen) {
      // offscreen未就绪,暂存resize参数
      this.pendingResize = { width, height };
      return;
    }
    if (this.offscreen.width === width && this.offscreen.height === height) return;
    this.offscreen.width = width;
    this.offscreen.height = height;
    // WebGL2需要显式更新viewport, WebGPU自动跟随
    if (this.renderer && typeof this.renderer.resize === 'function') {
      this.renderer.resize(width, height);
    }
    this.updateAutoSizeScale();
  }
  updateAutoSizeScale() {
    if (!this.autoSizeScale) return;
    if (!this.offscreen) return;
    // 需要视频源分辨率来计算scale
    const sourceInfo = this.player.sourceInfo;
    if (!sourceInfo || sourceInfo.videoSize() <= 0) return;
    const videoDesc = sourceInfo.getVideoDesc(0);
    const videoW = videoDesc.desc.width;
    const videoH = videoDesc.desc.height;
    const canvasW = this.offscreen.width;
    const canvasH = this.offscreen.height;
    if (videoW <= 0 || videoH <= 0 || canvasW <= 0 || canvasH <= 0) return;
    // aspect-fit: 视频在canvas中实际显示区域(类似CSS object-fit: contain)
    const videoAspect = videoW / videoH;
    const canvasAspect = canvasW / canvasH;
    let displayW, displayH;
    if (canvasAspect > videoAspect) {
      // canvas更宽, 视频按高度适配, 两侧黑边
      displayH = canvasH;
      displayW = canvasH * videoAspect;
    } else {
      // canvas更窄, 视频按宽度适配, 上下黑边
      displayW = canvasW;
      displayH = canvasW / videoAspect;
    }
    // 用实际显示区域与video的最小边比
    const ratio = Math.min(displayW / videoW, displayH / videoH);
    // console.log("ratio:", ratio);
    // 分级缩放: 画面越小,降scale越激进,省GPU/IO且不影响观感
    // 档位: 1.0 / 0.5 / 0.25 / 0.125
    let scale = 1.0;
    if (ratio <= 1 / 8) {
      scale = 0.125;
    } else if (ratio <= 1 / 4) {
      scale = 0.25;
    } else if (ratio <= 1 / 2) {
      scale = 0.5;
    }
    // 只在跨档时触发(天然防抖)
    if (scale === this.currentAutoScale) return;
    this.currentAutoScale = scale;
    const render = this.player.winRender;
    if (!render) return;
    if (scale < 1.0) {
      console.log(`Enabling size scale: ${scale}`);
      render.enableSizeScale(scale);
    } else {
      console.log(`Disabling size scale`);
      render.disableSizeChange();
    }
  }
  onFrame(frame) {
    if (!this.renderer) return;
    if (document.visibilityState === 'hidden') {
      return;
    }
    const { width, height, stride, frameSize, format } = frame;
    if (frameSize <= 0) return;
    // 通知渲染器视频尺寸变化(用于autoAspect计算)
    if (width !== this.lastFrameW || height !== this.lastFrameH) {
      this.lastFrameW = width;
      this.lastFrameH = height;
      if (typeof this.renderer.setVideoSize === 'function') {
        this.renderer.setVideoSize(width, height);
      }
    }
    const pbo = this.renderer.getBufferIfChanged(frameSize);
    if (pbo) {
      avox.setRenderJsBuffer(this.player.winOb, pbo);
      return;
    }
    // 只保存最新帧信息，rAF时再渲染，避免帧堆积
    this.pendingFrame = { width, height, stride, format };
    if (!this.rafPending) {
      this.rafPending = true;
      requestAnimationFrame(() => {
        this.rafPending = false;
        if (this.pendingFrame) {
          const f = this.pendingFrame;
          this.pendingFrame = null;
          this.renderer.renderPbo(f.width, f.height, f.stride, f.format === 1 ? 1 : 0);
        }
      });
    }
  }
  onSurface() {
  }
  destroy() {
    // 取消待渲染帧,避免 rAF 回调访问已销毁的 renderer
    this.pendingFrame = null;
    this.rafPending = false;
    this.pendingResize = null;
    // 释放 GL/WebGPU 资源,多路切换时避免 context/device 数量耗尽卡死
    if (this.renderer) {
      try { this.renderer.destroy(); } catch (e) { console.error('renderer destroy error:', e); }
      this.renderer = null;
    }
    this.offscreen = null;
  }
}

// JsRtcEventOb 类 - 处理 WebRTC 事件回调（基于 IRtcEventOb 接口）
class JsRtcEventOb {
  constructor(player) {
    this.player = player;
  }
  // IRtcEventOb 接口方法
  onConnectionState(state) {
    this.player.handleConnectionState(state);
  }
  onFirstVideoFrame() {
    this.player.triggerCallbacks('onFirstVideoFrame');
  }
  onDataChannelMsg(data, size) {
    this.player.handleDataChannelMsg(data, size);
  }
  onLocalSdp(sdp) {
    this.player.handleLocalSdp(sdp);
  }
  onIceCandidate(candidate, mid, lineIndex) {
    this.player.handleIceCandidate(candidate, mid, lineIndex);
  }
}

// JsRecorderOb 类 - 处理合成器回调（基于 IRecorderOb 接口，统一用于 MediaMuxer 和 Recorder）
class JsRecorderOb {
  constructor(owner) {
    this.owner = owner;
  }
  // IRecorderOb 接口方法
  onStateChange(preState, state) {
    this.owner.handleStateChange(preState, state);
  }
  onProgress(currentTimeMs, totalTimeMs) {
    this.owner.handleProgress(currentTimeMs, totalTimeMs);
  }
  onIoError(error, msg) {
    this.owner.handleIoError(error, msg);
  }
  onEncodeError(trackType, error) {
    this.owner.handleEncodeError(trackType, error);
  }
  onComplete() {
    this.owner.handleComplete();
  }
}

// MediaMuxer - 包装 native muxer 对象，提供方法封装和事件回调
class MediaMuxer {
  constructor(nativeMuxer) {
    if (!nativeMuxer) {
      console.error('MediaMuxer: nativeMuxer is null/undefined');
      return;
    }
    this.nativeMuxer = nativeMuxer;
    this.jsObservers = {};
    // 注册观察者
    try {
      const jsRecorderOb = new JsRecorderOb(this);
      this.muxerOb = avox.createJsRecorderOb(jsRecorderOb);
      avox.addMuxerOb(nativeMuxer, this.muxerOb);
    } catch (e) {
      console.error('MediaMuxer observer error:', e);
    }
  }
  on(event, callback) {
    if (!this.jsObservers[event]) {
      this.jsObservers[event] = [];
    }
    this.jsObservers[event].push(callback);
  }
  off(event) {
    delete this.jsObservers[event];
  }
  handleStateChange(preState, state) {
    const callbacks = this.jsObservers['onStateChange'];
    if (callbacks) callbacks.forEach(cb => { try { cb(preState, state); } catch (e) { console.error('muxer onStateChange error:', e); } });
  }
  handleProgress(currentTimeMs, totalTimeMs) {
    const callbacks = this.jsObservers['onProgress'];
    if (callbacks) callbacks.forEach(cb => { try { cb(currentTimeMs, totalTimeMs); } catch (e) { console.error('muxer onProgress error:', e); } });
  }
  handleIoError(error, msg) {
    const callbacks = this.jsObservers['onIoError'];
    if (callbacks) callbacks.forEach(cb => { try { cb(error, msg); } catch (e) { console.error('muxer onIoError error:', e); } });
  }
  handleEncodeError(trackType, error) {
    const callbacks = this.jsObservers['onEncodeError'];
    if (callbacks) callbacks.forEach(cb => { try { cb(trackType, error); } catch (e) { console.error('muxer onEncodeError error:', e); } });
  }
  handleComplete() {
    const callbacks = this.jsObservers['onComplete'];
    if (callbacks) callbacks.forEach(cb => { try { cb(); } catch (e) { console.error('muxer onComplete error:', e); } });
  }
  setHardEncode(hard) { this.nativeMuxer.setHardEncode(hard); }
  setVideoCodec(codecId) { this.nativeMuxer.setVideoCodec(codecId); }
  setAudioCodec(codecId) { this.nativeMuxer.setAudioCodec(codecId); }
  setMuxerType(type) { this.nativeMuxer.setMuxerType(type); }
  setAudioDesc(desc) {
    const audioDesc = new avox.AudioDesc();
    audioDesc.channels = desc.channels || 1;
    audioDesc.sampleRate = desc.sampleRate || 8000;
    audioDesc.format = desc.format || 2;  // AVOX_AUDIO_S16
    this.nativeMuxer.setAudioDesc(audioDesc);
  }
  open(url) { this.nativeMuxer.open(url); }
  close() { this.nativeMuxer.close(); }
  destroy() {
    if (this.muxerOb) {
      avox.removeMuxerOb(this.nativeMuxer, this.muxerOb);
      this.muxerOb = null;
    }
  }
}

let nextPlayerId = 1;
class BasePlayer {
  constructor() {
    this.playerId = nextPlayerId++;
    this.jsObservers = {};
    this.muxer = null;
    this.createNativePlayer();
    this.checkPlayerObj();
  }
  createNativePlayer() {
  }
  checkPlayerObj() {
    // 测试
    this.index = 0;
  }
  setOffscreen(offscreen) {
    this.jsWinOb.setOffscreen(offscreen);
  }
  on(event, callback) {
    // 支持多回调：存储为数组
    if (!this.jsObservers[event]) {
      this.jsObservers[event] = [];
    }
    this.jsObservers[event].push(callback);
    // console.log("on event:", event, "callbacks count:", this.jsObservers[event].length);
  }
  off(event) {
    delete this.jsObservers[event];
  }
  // 辅助方法：触发所有回调
  triggerCallbacks(event, ...args) {
    const callbacks = this.jsObservers[event];
    if (callbacks && Array.isArray(callbacks)) {
      callbacks.forEach(cb => {
        try {
          cb(...args);
        } catch (e) {
          console.error(`callback error for ${event}:`, e);
        }
      });
    }
  }
  handleStateChange(preState, state) {
    // console.log("preload handleStateChange:", preState, state);
    // 状态变为 closed 或 error 时清屏，避免绿屏
    if (state === 'closed' || state === 'error') {
      if (this.jsWinOb && this.jsWinOb.clearScreen) {
        this.jsWinOb.clearScreen();
      }
    }
    this.triggerCallbacks('onStateChange', preState, state);
  }
  handleIoError(error, msg) {
    // IO 错误时清屏，避免残留帧或绿屏
    if (this.jsWinOb && this.jsWinOb.clearScreen) {
      this.jsWinOb.clearScreen();
    }
    this.triggerCallbacks('onIoError', error, msg);
  }
  handleDecodeError(trackType, error) {
    // 解码错误时清屏
    if (this.jsWinOb && this.jsWinOb.clearScreen) {
      this.jsWinOb.clearScreen();
    }
    this.triggerCallbacks('onDecodeError', trackType, error);
  }
  handleReady() {
    this.sourceInfo = this.nplayer.getSourceInfo();
    if (this.sourceInfo.videoSize() > 0) {
      const videoDesc = this.sourceInfo.getVideoDesc(0);
      // 尽量申请大些
      const videoSize = videoDesc.desc.width * videoDesc.desc.height;
      // this.sharedData = new Uint8Array(videoSize*4);
      // console.log("videoResolution:", this.videoSize);
      // avox.setRenderJsBuffer(this.winOb, this.sharedData);
    }
    // 视频源分辨率已知,触发autoSizeScale计算
    if (this.jsWinOb) {
      this.jsWinOb.updateAutoSizeScale();
    }
    this.triggerCallbacks('onReady');
  }
  handleComplete() {
    this.triggerCallbacks('onComplete');
  }
  handleSeek() {
    this.triggerCallbacks('onSeek');
  }
  handlePause() {
    this.triggerCallbacks('onPause');
  }
  handleResume() {
    this.triggerCallbacks('onResume');
  }
  handleClose() {
    // 关闭时清屏
    if (this.jsWinOb && this.jsWinOb.clearScreen) {
      this.jsWinOb.clearScreen();
    }
    this.triggerCallbacks('onClose');
  }
}

class MediaPlayer extends BasePlayer {
  constructor() {
    super();
  }
  createNativePlayer() {
    this.nplayer = avox.createMediaPlayer();
    this.winRender = new SurfaceRender(this.nplayer.getSurfaceRender());
    this.audioRender = this.nplayer.getAudioRender();
    // YuvType_nv12 YuvType_yuv420P
    this.winRender.setOffSurface(avox.YuvType_yuv420P);
    const jsPlayerOb = new JsMediaPlayerOb(this);
    this.playerOb = avox.createJsMediaPlayerOb(jsPlayerOb);
    avox.addMediaPlayerOb(this.nplayer, this.playerOb);
    this.jsWinOb = new JsSurfaceRenderOb(this);
    this.winOb = avox.createJsSurfaceRenderOb(this.jsWinOb);
    avox.addSurfaceRenderOb(this.winRender.nativeSurfaceRender, this.winOb);
    const nmadiaMuxer = this.nplayer.getMuxer(false);
    if (nmadiaMuxer) {
      this.mediaMuxer = new MediaMuxer(nmadiaMuxer);
    }
    const nrawMuxer = this.nplayer.getMuxer(true);
    if (nrawMuxer) {
      this.rawMuxer = new MediaMuxer(nrawMuxer);
    }
  }
  // 如果录制的需要转码,叠加水印等,使用bTranscode = true
  getMuxer(bTranscode) {
    if (bTranscode) {
      return this.rawMuxer;
    }
    return this.mediaMuxer;
  }
  destroy() {
    if (this.winOb) {
      avox.removeSurfaceRenderOb(this.nplayer.getSurfaceRender(), this.winOb);
      this.winOb = null;
    }
    // removeSurfaceRenderOb 之后再释放 renderer(此时 native 不再回调 onFrame)
    if (this.jsWinOb) {
      this.jsWinOb.destroy();
      this.jsWinOb = null;
    }
    if (this.playerOb) {
      avox.removeMediaPlayerOb(this.nplayer, this.playerOb);
      this.playerOb = null;
    }
    // muxerOb 必须在 nplayer.close() 之前 remove(nativeMuxer 来自 nplayer.getMuxer())
    if (this.mediaMuxer) {
      this.mediaMuxer.destroy();
      this.mediaMuxer = null;
    }
    if (this.rawMuxer) {
      this.rawMuxer.destroy();
      this.rawMuxer = null;
    }
    if (this.nplayer) {
      this.nplayer.close();
      // 显式析构native播放器: GC finalizer回收正确但时机不可控, 高频开关下native内存
      // 滞留可累积至GB级(告警审核场景实测), destroyObject立即确定性释放
      if (typeof avox.destroyObject === 'function') {
        avox.destroyObject(this.nplayer);
      }
      this.nplayer = null;
      // winRender/audioRender持有player内部的借用指针, 随析构一起失效, 置空防复用
      this.winRender = null;
      this.audioRender = null;
    }
  }
}

// ========== WebRtcPlayer 封装类（基于 ISourcePlayer）==========
class WebRtcPlayer extends BasePlayer {
  constructor() {
    super();
  }
  createNativePlayer() {
    this.nplayer = avox.createWebRtcPlayer();
    this.winRender = new SurfaceRender(this.nplayer.getRemoteSurfaceRender());
    this.audioRender = this.nplayer.getRemoteAudioRender();
    this.winRender.setOffSurface(avox.YuvType_nv12);
    const jsPlayerOb = new JsMediaPlayerOb(this);
    this.playerOb = avox.createJsMediaPlayerOb(jsPlayerOb);
    avox.addRtcPlayerOb(this.nplayer, this.playerOb);
    this.jsWinOb = new JsSurfaceRenderOb(this);
    this.winOb = avox.createJsSurfaceRenderOb(this.jsWinOb);
    avox.addSurfaceRenderOb(this.winRender.nativeSurfaceRender, this.winOb);
    const jsRtcEventOb = new JsRtcEventOb(this);
    this.rtcEventOb = avox.createJsRtcEventOb(jsRtcEventOb);
    this.nplayer.addOb(this.rtcEventOb);
  }
  setRollType(answer) {
    let rollType = avox.RtcRollType_offer;
    if (answer) {
      rollType = avox.RtcRollType_answer;
    }
    avox.logMsg(0, `createWebRtcPlayer: ${rollType}`);
    this.nplayer.setRollType(rollType);
  }
  handleConnectionState(state) {
    this.triggerCallbacks('onConnectionState', state);
  }
  handleDataChannelMsg(data, size) {
    this.triggerCallbacks('onDataChannelMsg', data, size);
  }
  handleLocalSdp(sdp) {
    avox.logMsg(0, `jsob sdp local sdp: ${sdp}`);
    // 直接触发到 JS 层，避免双重处理
    this.triggerCallbacks('onLocalSdp', sdp);
  }
  handleIceCandidate(candidate, mid, lineIndex) {
    avox.logMsg(0, `jsob sdp ice candidate: ${candidate} mid: ${mid} lineIndex: ${lineIndex}`);
    this.triggerCallbacks('onIceCandidate', candidate, mid, lineIndex);
  }
  destroy() {
    if (this.rtcEventOb) {
      this.nplayer.removeOb(this.rtcEventOb);
      this.rtcEventOb = null;
    }
    if (this.winOb) {
      // 复用 create 时绑定的同一个 ISurfaceRender(IRtcPlayer 无 getSurfaceRender)
      avox.removeSurfaceRenderOb(this.winRender.nativeSurfaceRender, this.winOb);
      this.winOb = null;
    }
    // removeSurfaceRenderOb 之后再释放 renderer(此时 native 不再回调 onFrame)
    if (this.jsWinOb) {
      this.jsWinOb.destroy();
      this.jsWinOb = null;
    }
    if (this.playerOb) {
      avox.removeRtcPlayerOb(this.nplayer, this.playerOb);
      this.playerOb = null;
    }
    if (this.nplayer) {
      this.nplayer.close();
      // 显式析构: 同 MediaPlayer destroy(), 避免 GC 时机不可控导致 native 内存滞留
      if (typeof avox.destroyObject === 'function') {
        avox.destroyObject(this.nplayer);
      }
      this.nplayer = null;
    }
  }
}

// ========== SourcePlayer 封装类（基于 ISourcePlayer，音频推流）==========
class SourcePlayer extends BasePlayer {
  constructor() {
    super();
  }
  createNativePlayer() {
    this.nplayer = avox.createDevicePlayer();
    this.winRender = new SurfaceRender(this.nplayer.getSurfaceRender());
    this.audioRender = this.nplayer.getAudioRender();
    this.audioMgr = avox.getAudioManager(avox.ADeviceSdk_wasapi);
    // 使用辅助函数获取音频设备（避免SWIG重载函数问题）
    if (this.audioMgr) {
      try {
        const count = this.audioMgr.getDeviceCount();
        if (count > 0) {
          const device = this.audioMgr.getDevice(0);
          if (device) {
            this.nplayer.setAudioSource(device);
          }
        }
      } catch (e) {
        console.error('[SourcePlayer] Failed to set audio source:', e);
      }
    }
    const jsPlayerOb = new JsMediaPlayerOb(this);
    this.playerOb = avox.createJsMediaPlayerOb(jsPlayerOb);
    avox.addSourcePlayerOb(this.nplayer, this.playerOb);
    const nativeMuxer = this.nplayer.getMuxer();
    if (nativeMuxer) {
      this.muxer = new MediaMuxer(nativeMuxer);
    }
  }
  startPush(url, muxerConfig) {
    if (!this.muxer) {
      console.error('SourcePlayer.startPush: muxer not available');
      return;
    }
    if (muxerConfig) {
      if (muxerConfig.hardEncode !== undefined) this.muxer.setHardEncode(muxerConfig.hardEncode);
      if (muxerConfig.videoCodecId !== undefined) this.muxer.setVideoCodec(muxerConfig.videoCodecId);
      if (muxerConfig.audioCodecId !== undefined) this.muxer.setAudioCodec(muxerConfig.audioCodecId);
      if (muxerConfig.muxerType !== undefined) this.muxer.setMuxerType(muxerConfig.muxerType);
      if (muxerConfig.sampleRate !== undefined || muxerConfig.channels !== undefined) {
        this.muxer.setAudioDesc({
          sampleRate: muxerConfig.sampleRate || 8000,
          channels: muxerConfig.channels || 1,
          format: 2
        });
      }
    }
    if (url) {
      this.on('onReady', () => {
        this.muxer.open(url);
      });
    }
    this.nplayer.open();
  }
  stopPush() {
    if (this.muxer) this.muxer.close();
    this.nplayer.close();
  }
  destroy() {
    if (this.playerOb) {
      avox.removeSourcePlayerOb(this.nplayer, this.playerOb);
      this.playerOb = null;
    }
    // muxerOb 必须在 nplayer.close() 之前 remove(nativeMuxer 来自 nplayer.getMuxer())
    if (this.muxer) {
      this.muxer.destroy();
      this.muxer = null;
    }
    if (this.nplayer) {
      this.nplayer.close();
      // 显式析构: 同 MediaPlayer destroy(), 避免 GC 时机不可控导致 native 内存滞留
      if (typeof avox.destroyObject === 'function') {
        avox.destroyObject(this.nplayer);
      }
      this.nplayer = null;
    }
    this.audioMgr = null;
  }
}

// ========== Recorder 封装类（基于 IRecorder）==========
let nextRecorderId = 1;
class Recorder {
  constructor(bTranscode = false) {
    this.recorderId = nextRecorderId++;
    this.bTranscode = bTranscode;
    this.jsObservers = {};
    this.nrecorder = avox.createRecorder(bTranscode);
    const jsRecorderOb = new JsRecorderOb(this);
    this.recorderOb = avox.createJsRecorderOb(jsRecorderOb);
    avox.addRecorderOb(this.nrecorder, this.recorderOb);
    // 转码模式下获取 SurfaceRender 用于图像处理
    if (bTranscode) {
      this.surfaceRender = new SurfaceRender(this.nrecorder.getSurfaceRender());
    }
  }
  on(event, callback) {
    // 支持多回调
    if (!this.jsObservers[event]) {
      this.jsObservers[event] = [];
    }
    this.jsObservers[event].push(callback);
  }
  off(event) {
    delete this.jsObservers[event];
  }
  triggerCallbacks(event, ...args) {
    const callbacks = this.jsObservers[event];
    if (callbacks && Array.isArray(callbacks)) {
      callbacks.forEach(cb => {
        try {
          cb(...args);
        } catch (e) {
          console.error(`callback error for ${event}:`, e);
        }
      });
    }
  }
  handleIoError(error, msg) {
    this.triggerCallbacks('onIoError', error, msg);
  }
  handleStateChange(preState, state) {
    this.triggerCallbacks('onStateChange', preState, state);
  }
  handleProgress(currentTimeMs, totalTimeMs) {
    this.triggerCallbacks('onProgress', currentTimeMs, totalTimeMs);
  }
  handleEncodeError(trackType, error) {
    this.triggerCallbacks('onEncodeError', trackType, error);
  }
  handleComplete() {
    this.triggerCallbacks('onComplete');
  }
  seek(posMs) {
    if (!this.nrecorder) return false;
    return this.nrecorder.seek(posMs);
  }
  getDuration() {
    if (!this.nrecorder) return 0;
    return this.nrecorder.getDuration();
  }
  getSourceInfo() {
    if (!this.nrecorder) return null;
    return wrapSourceInfo(this.nrecorder.getSourceInfo());
  }
  destroy() {
    if (this.recorderOb) {
      avox.removeRecorderOb(this.nrecorder, this.recorderOb);
      this.recorderOb = null;
    }
    if (this.nrecorder) {
      this.nrecorder.close();
      delete this.nrecorder;
      this.nrecorder = null;
    }
  }
}

wrapOption = (option) => {
  return {
    setInt: (key, value) => option.setInt(key, value),
    setString: (key, value) => option.setString(key, value),
    setBool: (key, value) => option.setBool(key, value),
    setNumber: (key, value) => option.setNumber(key, value),
  }
}

wrapFontLayer = (fontLayer) => {
  if (!fontLayer) return null;
  return {
    updateLayout: (index, layout) => {
      const fontLayout = new avox.FontLayout();
      fontLayout.x = layout.x || 0;
      fontLayout.y = layout.y || 0;
      fontLayout.width = layout.width || 0;
      fontLayout.height = layout.height || 0;
      const alignment = new avox.Alignment();
      alignment.horizontal = layout.horizontal || 0;
      alignment.vertical = layout.vertical || 0;
      fontLayout.alignment = alignment;
      fontLayer.updateLayout(index, fontLayout);
    },
    setTextLayout: (index) => fontLayer.setTextLayout(index),
    setColor: (r, g, b, opacity) => fontLayer.setColor(r, g, b, opacity || 0),
    drawText: (text) => fontLayer.drawText(text),
    setScale: (scale) => fontLayer.setScale(scale),
  };
}

// SurfaceRender - 包装 native windowRender 对象，管理字体层等资源
class SurfaceRender {
  constructor(nativeSurfaceRender) {
    if (!nativeSurfaceRender) {
      console.error('SurfaceRender: nativeSurfaceRender is null/undefined');
      return;
    }
    this.nativeSurfaceRender = nativeSurfaceRender;
    this.fontLayer = null;
  }
  setOffSurface(type) { this.nativeSurfaceRender.setOffSurface(type); }
  screenShot(imageBuffer) { return this.nativeSurfaceRender.screenShot(imageBuffer); }
  enableWatermark(watermark, imageBuffer) { this.nativeSurfaceRender.enableWatermark(watermark, imageBuffer); }
  disableWatermark() { this.nativeSurfaceRender.disableWatermark(); }
  enableLut(lutParamet) { this.nativeSurfaceRender.enableLut(lutParamet); }
  disableLut() { this.nativeSurfaceRender.disableLut(); }
  enableSizeScale(scale) { this.nativeSurfaceRender.enableSizeScale(scale); }
  disableSizeChange() { this.nativeSurfaceRender.disableSizeChange(); }
  enableFont() {
    // 每次都重新启用渲染管线
    const nativeFontLayer = avox.enableRenderFont(this.nativeSurfaceRender);
    this.fontLayer = wrapFontLayer(nativeFontLayer);
    return this.fontLayer;
  }
  disableFont() {
    avox.disableRenderFont(this.nativeSurfaceRender);
  }
  // 亮度 (-1.0 ~ 1.0)
  updateBrightness(value) { this.nativeSurfaceRender.updateBrightness(value); }
  disableBrightness() { this.nativeSurfaceRender.disableBrightness(); }
  // 对比度 (0.0 ~ 2.0)
  updateContrast(value) { this.nativeSurfaceRender.updateContrast(value); }
  disableContrast() { this.nativeSurfaceRender.disableContrast(); }
  // 饱和度 (0.0 ~ 2.0)
  updateSaturation(value) { this.nativeSurfaceRender.updateSaturation(value); }
  disableSaturation() { this.nativeSurfaceRender.disableSaturation(); }
  // 锐度
  updateSharpen(offset, sharpness) {
    const paramet = new avox.SharpenVideo();
    paramet.offset = offset;
    paramet.sharpness = sharpness;
    this.nativeSurfaceRender.updateSharpen(paramet);
  }
  disableSharpen() { this.nativeSurfaceRender.disableSharpen(); }
}

wrapSurfaceRender = (winRender) => {
  if (!winRender) return null;
  return {
    setOffSurface: (yuvType) => winRender.setOffSurface(yuvType),
    screenShot: (imageBuffer) => winRender.screenShot(imageBuffer),
    screenShotToPath: (imagePath) => {
      const imageBuffer = avox.createImageBuffer();
      if (winRender.screenShot(imageBuffer)) {
        return avox.saveImagePath(imagePath, imageBuffer);
      }
      return false;
    },
    screenShotToBase64: (encodeType, quality) => {
      const imageBuffer = avox.createImageBuffer();
      if (winRender.screenShot(imageBuffer)) {
        const config = new avox.IEncodeConfig();
        config.encodeType = encodeType != null ? encodeType : avox.IEncodeType_jpg;
        config.quality = quality != null ? quality : 85;
        return avox.getImageBase64(imageBuffer, config);
      }
      return null;
    },
    enableWatermark: (wparamet, assetsPath) => {
      let watermark = new avox.Watermark();
      watermark.centerX = wparamet.centerX;
      watermark.centerY = wparamet.centerY;
      watermark.width = wparamet.width;
      watermark.height = wparamet.height;
      watermark.alaph = wparamet.alaph;
      const imageBuffer = avox.createImageBuffer();
      const bLoad = avox.loadImageAsset(assetsPath, imageBuffer);
      if (bLoad) {
        winRender.enableWatermark(watermark, imageBuffer);
      } else {
        console.log("Failed to load watermark image:", assetsPath);
      }
      return bLoad;
    },
    disableWatermark: () => winRender.disableWatermark(),
    enableLut: (index) => {
      let lutParamet = new avox.LutParamet();
      lutParamet.lutIndex = index;
      winRender.enableLut(lutParamet);
    },
    disableLut: () => winRender.disableLut(),
    enableFont: () => winRender.enableFont(),
    disableFont: () => winRender.disableFont(),
    enableSizeScale: (scale) => winRender.enableSizeScale(scale),
    disableSizeChange: () => winRender.disableSizeChange(),
    // 亮度/对比度/饱和度/锐度
    updateBrightness: (value) => winRender.updateBrightness(value),
    disableBrightness: () => winRender.disableBrightness(),
    updateContrast: (value) => winRender.updateContrast(value),
    disableContrast: () => winRender.disableContrast(),
    updateSaturation: (value) => winRender.updateSaturation(value),
    disableSaturation: () => winRender.disableSaturation(),
    updateSharpen: (offset, sharpness) => winRender.updateSharpen(offset, sharpness),
    disableSharpen: () => winRender.disableSharpen(),
  };
}

wrapAudioRender = (audioRender) => {
  return {
    setVolume: (volume) => audioRender.setVolume(volume),
    getVolume: () => audioRender.getVolume(),
    enableAec: (aec) => audioRender.enableAec(aec),
    disableAec: () => audioRender.disableAec(),
  }
}

wrapMediaMuxer = (muxer) => {
  if (!muxer) return null;
  return {
    on: (event, callback) => muxer.on(event, callback),
    off: (event) => muxer.off(event),
    setHardEncode: (hard) => muxer.setHardEncode(hard),
    setVideoCodec: (codecId) => muxer.setVideoCodec(codecId),
    setAudioCodec: (codecId) => muxer.setAudioCodec(codecId),
    setMuxerType: (type) => muxer.setMuxerType(type),
    setAudioDesc: (desc) => muxer.setAudioDesc(desc),
    open: (url) => muxer.open(url),
    close: () => muxer.close(),
    getState: () => muxer.getState(),
    destroy: () => muxer.destroy(),
  };
}

wrapSourceInfo = (sourceInfo) => {
  return {
    videoSize: () => sourceInfo.videoSize(),
    audioSize: () => sourceInfo.audioSize(),
    getVideoDesc: (index) => {
      const vtrackDesc = sourceInfo.getVideoDesc(index);
      return {
        trackId: vtrackDesc.trackId,
        codecId: vtrackDesc.codecId,
        desc: {
          width: vtrackDesc.desc.width,
          height: vtrackDesc.desc.height,
          fps: vtrackDesc.desc.fps,
          type: vtrackDesc.desc.type
        }
      };
    },
    getAudioDesc: (index) => {
      const atrackDesc = sourceInfo.getAudioDesc(index);
      return {
        trackId: atrackDesc.trackId,
        codecId: atrackDesc.codecId,
        desc: {
          channels: atrackDesc.desc.channels,
          sampleRate: atrackDesc.desc.sampleRate,
          format: atrackDesc.desc.format
        }
      };
    }
  };
}

// 播放器实例管理
const playerInstances = new Map();
// 日志对象
const jsLogOb = new JsLogOb();
let nativeLogOb = null;

// 通过 contextBridge 安全暴露 API 到渲染进程
contextBridge.exposeInMainWorld('avox', {
  // ========== 播放器工厂方法 ==========
  createMediaPlayer: () => {
    try {
      const player = new MediaPlayer();
      playerInstances.set(player.playerId, player);
      console.log(`mediaPlayer created for ${player.playerId}`);
      // 对照avox/AvoxPlayer.h里IMediaPlayer提供C++/js的渲染进程可访问接口
      return {
        playerId: player.playerId,
        on: (event, callback) => player.on(event, callback),
        off: (event) => player.off(event),
        // 测试,拿到canvas,渲染进程转移所有权到preload.js
        setOffscreen: (offscreen) => player.setOffscreen(offscreen),
        // 方法
        getOption: () => wrapOption(player.nplayer.getOption()),
        setIoPlan: (plan) => player.nplayer.setIoPlan(plan),
        setHardDecode: (enable) => {
          player.nplayer.setHardDecode(enable);
        },
        setVulkan: (enable) => player.nplayer.getSurfaceRender().setVulkan(enable),
        getSurfaceRender: () => wrapSurfaceRender(player.winRender),
        getAudioRender: () => wrapAudioRender(player.nplayer.getAudioRender()),
        getMuxer: (bTranscode) => wrapMediaMuxer(player.getMuxer(bTranscode)),
        open: (url) => player.nplayer.open(url),
        close: () => player.nplayer.close(),
        seek: (time) => player.nplayer.seek(time),
        pause: () => player.nplayer.pause(),
        resume: () => player.nplayer.resume(),
        speed: (speed) => player.nplayer.speed(speed),
        getState: () => player.nplayer.getState(),
        getProcess: () => player.nplayer.getProcess(),
        getDuration: () => player.nplayer.getDuration(),
        getPosition: () => player.nplayer.getPosition(),
        getStartTime: () => player.nplayer.getStartTime(),
        getSourceInfo: () => {
          return wrapSourceInfo(player.nplayer.getSourceInfo());
        },
        getRate: (type, bAvg) => player.nplayer.getRate(type, bAvg || false),
        getLossRate: (type) => player.nplayer.getLossRate(type),
        getFps: () => player.nplayer.getFps(),
        destroy: () => {
          player.destroy();
          // 异步销毁时同 playerId 可能已被新实例占用,仅当 Map 里仍是本实例才删除
          if (playerInstances.get(player.playerId) === player) {
            playerInstances.delete(player.playerId);
          }
        },
        setAutoSizeScale: (enabled) => {
          if (player.jsWinOb) {
            player.jsWinOb.autoSizeScale = enabled;
            if (enabled) {
              player.jsWinOb.updateAutoSizeScale();
            }
          }
        },
        getAutoSizeScale: () => {
          if (player.jsWinOb) {
            return player.jsWinOb.autoSizeScale;
          }
          return false;
        },
        setAutoAspect: (enabled) => {
          if (player.jsWinOb) {
            player.jsWinOb.autoAspect = enabled;
            if (player.jsWinOb.renderer && typeof player.jsWinOb.renderer.setAutoAspect === 'function') {
              player.jsWinOb.renderer.setAutoAspect(enabled);
            }
          }
        },
        getAutoAspect: () => {
          if (player.jsWinOb) {
            return player.jsWinOb.autoAspect;
          }
          return true;
        },
        resizeCanvas: (width, height) => {
          if (player.jsWinOb) {
            player.jsWinOb.resize(width, height);
          }
        },
        setZoomPan: (zoom, panX, panY) => {
          if (player.jsWinOb && player.jsWinOb.renderer) {
            player.jsWinOb.renderer.setZoomPan(zoom, panX, panY);
          }
        }
      };
    } catch (error) {
      console.error('failed to create MediaPlayer:', error);
      return null;
    }
  },
  createWebRtcPlayer: () => {
    try {
      const player = new WebRtcPlayer();
      playerInstances.set(player.playerId, player);
      // 对照avox/AvoxPlayer.h里ISourcePlayer提供C++/js的渲染进程可访问接口
      return {
        playerId: player.playerId,
        on: (event, callback) => player.on(event, callback),
        off: (event) => player.off(event),
        setOffscreen: (offscreen) => player.setOffscreen(offscreen),
        // 方法
        getRemoteSurfaceRender: () => wrapSurfaceRender(player.winRender),
        getAudioRender: () => wrapAudioRender(player.nplayer.getRemoteAudioRender()),
        getMuxer: () => wrapMediaMuxer(player.muxer),
        open: () => {
          player.nplayer.open();
        },
        close: () => player.nplayer.close(),
        getState: () => player.nplayer.getState(),
        getSourceInfo: () => {
          return wrapSourceInfo(player.nplayer.getRemoteSourceInfo());
        },
        // avox_webrtc/RtcExport.h
        setRollType: (answer) => player.setRollType(answer),
        getLocalSdp: () => player.nplayer.getLocalSdp(),
        setRemoteSdp: (sdp) => player.nplayer.setRemoteSdp(sdp),
        addIceCandidate: (candidate, sdpMid, sdpMLineIndex) => player.nplayer.addIceCandidate(candidate, sdpMid, sdpMLineIndex),
        // zoom/pan/resize: WebRtcPlayer 同样继承 BasePlayer,暴露面须与 createMediaPlayer 一致
        setAutoSizeScale: (enabled) => {
          if (player.jsWinOb) {
            player.jsWinOb.autoSizeScale = enabled;
            if (enabled) {
              player.jsWinOb.updateAutoSizeScale();
            }
          }
        },
        getAutoSizeScale: () => {
          if (player.jsWinOb) {
            return player.jsWinOb.autoSizeScale;
          }
          return false;
        },
        setAutoAspect: (enabled) => {
          if (player.jsWinOb) {
            player.jsWinOb.autoAspect = enabled;
            if (player.jsWinOb.renderer && typeof player.jsWinOb.renderer.setAutoAspect === 'function') {
              player.jsWinOb.renderer.setAutoAspect(enabled);
            }
          }
        },
        getAutoAspect: () => {
          if (player.jsWinOb) {
            return player.jsWinOb.autoAspect;
          }
          return true;
        },
        resizeCanvas: (width, height) => {
          if (player.jsWinOb) {
            player.jsWinOb.resize(width, height);
          }
        },
        setZoomPan: (zoom, panX, panY) => {
          if (player.jsWinOb && player.jsWinOb.renderer) {
            player.jsWinOb.renderer.setZoomPan(zoom, panX, panY);
          }
        },
        destroy: () => {
          player.destroy();
          // 异步销毁时同 playerId 可能已被新实例占用,仅当 Map 里仍是本实例才删除
          if (playerInstances.get(player.playerId) === player) {
            playerInstances.delete(player.playerId);
          }
        }
      };
    } catch (error) {
      console.error('failed to create WebRtcPlayer:', error);
      return null;
    }
  },
  createSourcePlayer: () => {
    try {
      const player = new SourcePlayer();
      playerInstances.set(player.playerId, player);
      // 音频推流：只暴露必要的控制接口，音频设备管理已内部处理
      return {
        playerId: player.playerId,
        on: (event, callback) => player.on(event, callback),
        off: (event) => player.off(event),
        getAudioRender: () => wrapAudioRender(player.nplayer.getAudioRender()),
        getMuxer: () => wrapMediaMuxer(player.muxer),
        startPush: (url, muxerConfig) => player.startPush(url, muxerConfig),
        stopPush: () => player.stopPush(),
        open: () => player.nplayer.open(),
        close: () => player.nplayer.close(),
        getState: () => player.nplayer.getState(),
        getAudioDeviceCount: () => player.audioMgr ? player.audioMgr.getDeviceCount() : 0,
        getAudioDeviceName: (index) => {
          if (!player.audioMgr) return '';
          try {
            const device = player.audioMgr.getDevice(index);
            return device ? device.getDeviceName() : '';
          } catch (e) {
            return '';
          }
        },
        destroy: () => {
          player.destroy();
          // 异步销毁时同 playerId 可能已被新实例占用,仅当 Map 里仍是本实例才删除
          if (playerInstances.get(player.playerId) === player) {
            playerInstances.delete(player.playerId);
          }
        }
      };
    } catch (error) {
      console.error('failed to create SourcePlayer:', error);
      return null;
    }
  },
  createRecorder: (bTranscode = false) => {
    try {
      const recorder = new Recorder(bTranscode);
      const result = {
        recorderId: recorder.recorderId,
        on: (event, callback) => recorder.on(event, callback),
        off: (event) => recorder.off(event),
        setIoPlan: (plan) => recorder.nrecorder.setIoPlan(plan),
        // RTSP拉流倍速(仅zlmediakit IO生效;点播/回放源4x全帧率,8x起服务端抽帧)
        setSpeed: (speed) =>
          recorder.nrecorder.getOption().setNumber('io.rtsp.speed', speed),
        setMuxerType: (type) => recorder.nrecorder.setMuxerType(type),
        setVideoCodec: (codecId) => recorder.nrecorder.setVideoCodec(codecId),
        setAudioCodec: (codecId) => recorder.nrecorder.setAudioCodec(codecId),
        open: (inputUrl, outputFile) => recorder.nrecorder.open(inputUrl, outputFile),
        close: () => recorder.nrecorder.close(),
        getState: () => recorder.nrecorder.getState(),
        destroy: () => recorder.destroy()
      };
      // 转码模式下暴露 SurfaceRender 用于图像处理（缩放、水印、LUT等）
      if (bTranscode) {
        result.getSurfaceRender = () => wrapSurfaceRender(recorder.surfaceRender);
      }
      return result;
    } catch (error) {
      console.error('failed to create Recorder:', error);
      return null;
    }
  },
  nativeLog: (enable, logFn) => {
    if (enable) {
      jsLogOb.logFn = logFn;
      if (!nativeLogOb) {
        nativeLogOb = avox.createJsLogOb(jsLogOb);
        avox.setLogObserver(nativeLogOb);
      }
    } else {
      jsLogOb.logFn = null;
      nativeLogOb = null;
      avox.setLogObserver(null);
    }
  },
  // ========== 日志方法 ==========
  logMsg: (level, message) => avox.logMsg(level, message),
  // ========== 字符串转换 ==========
  getPlayerStateStr: (state) => avox.getPlayerStateStr(state),
  getIoPlanStr: (plan) => avox.getIoPlanStr(plan),
  getVCodecName: (codecId) => avox.getVCodecName(codecId),
  getACodecName: (codecId) => avox.getACodecName(codecId),
  getYuvTypeStr: (yuvType) => avox.getYuvTypeStr(yuvType),
  getAudioFormatStr: (format) => avox.getAudioFormatStr(format),
  // ========== Vulkan ==========
  canVulkan: () => avox.canVulkan(),
  // ========== 常量 ==========
  IoPlan_zlmediakit: avox.IoPlan_zlmediakit,
  IoPlan_ffmpeg: avox.IoPlan_ffmpeg,
  YuvType_nv12: avox.YuvType_nv12,
  YuvType_yuv420P: avox.YuvType_yuv420P,
  MuxerType_ffmpeg: avox.MuxerType_ffmpeg,
  MuxerType_zlmediakit: avox.MuxerType_zlmediakit,
  MuxerType_onvif: avox.MuxerType_onvif,
  // ========== ADeviceSdk 常量 ==========
  ADeviceSdk_none: avox.ADeviceSdk_none,
  ADeviceSdk_wasapi: avox.ADeviceSdk_wasapi,
  ADeviceSdk_android: avox.ADeviceSdk_android,
  ADeviceSdk_ios: avox.ADeviceSdk_ios,
  // ========== RecorderState 常量 ==========
  RecorderState_none: avox.RecorderState_none,
  RecorderState_opening: avox.RecorderState_opening,
  RecorderState_recording: avox.RecorderState_recording,
  RecorderState_completed: avox.RecorderState_completed,
  // ========== 结构体 ==========
  WindowParamet: avox.WindowParamet,
  Watermark: avox.Watermark,
  LutParamet: avox.LutParamet,
  // ========== 播放器管理 ==========
  getPlayerCount: () => {
    let count = 0;
    for (const player of playerInstances.values()) {
      if (player instanceof MediaPlayer) count++;
    }
    return count;
  },
  getPlayerIndex: (playerId) => {
    let index = 0;
    for (const [id, player] of playerInstances) {
      if (!(player instanceof MediaPlayer)) continue;
      if (id === playerId) return index;
      index++;
    }
    return -1;
  },
  // ========== ONVIF Backchannel 检测 ==========
  checkOnvif: (url) => {
    try {
      const trackDesc = new avox.ATrackDesc();
      const result = avox.checkOnvif(url, trackDesc);
      if (result) {
        return {
          isOnvifBackchannel: true,
          audioDesc: {
            codecId: trackDesc.codecId,
            sampleRate: trackDesc.desc.sampleRate,
            channels: trackDesc.desc.channels,
            format: trackDesc.desc.format
          }
        };
      }
      return { isOnvifBackchannel: false };
    } catch (e) {
      console.error('checkOnvif error:', e);
      return { isOnvifBackchannel: false, error: e.message };
    }
  },

  // ========== avox_agent ==========
  //
  // 设计: 这里只暴露工厂 + 顶层 C 函数。host/session 拿到的是 SWIG ObjectWrap 包装的
  // C++ 指针, 经 contextBridge 进入渲染进程时会自动变成 proxy (其方法经 IPC 转发到主进程
  // 调用真正的 napi wrapper)。所以 addObserver / followup / shutdown 等方法不用逐一登记,
  // 渲染进程拿到 host/session 后直接 host.openAgent(...) 即可。
  //
  // 唯一注意**审批 ask**: C++ 侧走 BlockingCall 同步等 JS 返回 int。**在
  // ---- avox_agent 离散 IPC: host/session 状态在主进程持有 ----
  // 渲染进程只调这些, 不直接拿 native 句柄 (contextBridge 不复制 prototype 方法).
  createAgentHost: (configJson) => {
    if (nativeHost) return { ok: false, error: 'host already exists; shutdown first' };
    try {
      const cfgStr = typeof configJson === 'string'
        ? configJson : JSON.stringify(configJson || {});
      const host = avox.createAgentHost(cfgStr);
      if (!host) {
        let err = '';
        try { err = avox.lastAgentHostError(); } catch (_) {}
        return { ok: false, error: err || 'createAgentHost returned null' };
      }
      nativeHost = host;
      promptSections.length = 0;
      approvalUiInstalled = false;
      pendingApprovalAnswer = -1;
      return { ok: true };
    } catch (e) {
      return { ok: false, error: String(e && e.message || e) };
    }
  },
  hasHost: () => !!nativeHost,
  hostShutdown: () => {
    if (!nativeHost) return { ok: true };
    try { nativeHost.shutdown(); } catch (_) {}
    nativeHost = null;
    nativeSession = null;
    observerHandles.clear();
    return { ok: true };
  },
  hostLastError: () => {
    if (!nativeHost) return '';
    try { return nativeHost.lastError() || ''; }
    catch (_) { return lastErrorCache || ''; }
  },
  hostAddPromptSection: (name, order, text) => {
    if (!nativeHost) return { ok: false, error: 'no host' };
    try {
      const ok = !!nativeHost.addPromptSection(name || '', order | 0,
                                                text == null ? '' : String(text));
      if (ok) promptSections.push({ name, order, text });
      return { ok };
    } catch (e) {
      return { ok: false, error: String(e && e.message || e) };
    }
  },
  // 审批 UI: askHandler 是 renderer 传入的函数, 仅作日志; 真正答案靠 stageApprovalAnswer.
  hostSetApprovalUi: (askHandler) => {
    if (!nativeHost) return { ok: false, error: 'no host' };
    try {
      if (approvalUiInstalled) {
        // 二次安装直接忽略 (host 只有一个 approval slot)
        return { ok: true };
      }
      const wrapper = new JsApprovalUi(askHandler);
      const nativeUi = avox.createJsApprovalUiOb(wrapper);
      nativeHost.setApprovalUi(nativeUi);
      approvalUiInstalled = true;
      return { ok: true };
    } catch (e) {
      return { ok: false, error: String(e && e.message || e) };
    }
  },
  // 渲染进程预置下一次 ask 的答案 (主进程 ask() 时取走).
  // 0=ALLOW_ONCE 1=REJECT 2=CANCEL 3=UNAVAILABLE; -1=清空.
  stageApprovalAnswer: (answer) => {
    pendingApprovalAnswer = (answer | 0);
    return true;
  },
  // 生命周期: openAgent 返回 trackPath 或错误
  hostOpenAgent: (sessionId) => {
    if (!nativeHost) return { ok: false, error: 'no host' };
    if (nativeSession) return { ok: false, error: 'session already open' };
    try {
      const s = nativeHost.openAgent(sessionId || '');
      if (!s) {
        let err = '';
        try { err = nativeHost.lastError(); } catch (_) {}
        return { ok: false, error: err || 'openAgent returned null' };
      }
      nativeSession = s;
      let track = '';
      try { track = s.trackPath(); } catch (_) {}
      return { ok: true, trackPath: track };
    } catch (e) {
      return { ok: false, error: String(e && e.message || e) };
    }
  },
  hostCloseAgent: () => {
    if (!nativeSession || !nativeHost) return { ok: true };
    try { nativeHost.closeAgent(); } catch (_) {}
    nativeSession = null;
    observerHandles.clear();
    return { ok: true };
  },
  hasSession: () => !!nativeSession,
  // 三条输入通道
  sessionFollowup: (text) => {
    if (!nativeSession) return false;
    try { nativeSession.followup(text == null ? '' : String(text)); return true; }
    catch (_) { return false; }
  },
  // 带图 followup: images = [{base64(不带 data: 前缀), mediaType, name?}]。
  // 返回 {ok, error}; 限额/格式拒绝的原因在 error (来自 C++ lastInputError)。
  sessionFollowupImages: (text, images) => {
    if (!nativeSession) return { ok: false, error: 'no session' };
    try {
      const r = nativeSession.followupImages(
        text == null ? '' : String(text),
        JSON.stringify(Array.isArray(images) ? images : []));
      if (r === 0) return { ok: true };
      let err = '';
      try { err = nativeSession.lastInputError() || ''; } catch (_) {}
      return { ok: false, error: err || '发送失败' };
    } catch (e) {
      return { ok: false, error: String(e && e.message || e) };
    }
  },
  sessionSteer: (text) => {
    if (!nativeSession) return false;
    try { nativeSession.steer(text == null ? '' : String(text)); return true; }
    catch (_) { return false; }
  },
  sessionInject: (text) => {
    if (!nativeSession) return false;
    try { nativeSession.inject(text == null ? '' : String(text)); return true; }
    catch (_) { return false; }
  },
  sessionCancel: (cause) => {
    if (!nativeSession) return false;
    try { nativeSession.cancel((cause | 0)); return true; }
    catch (_) { return false; }
  },
  sessionWaitIdle: (timeoutMs) => {
    if (!nativeSession) return false;
    try { return !!nativeSession.waitIdle(timeoutMs | 0); }
    catch (_) { return false; }
  },
  sessionStatus: () => {
    if (!nativeSession) return 0;
    try { return nativeSession.status() | 0; } catch (_) { return 0; }
  },
  sessionCompactNow: () => {
    if (!nativeSession) return false;
    try { return !!nativeSession.compactNow(); } catch (_) { return false; }
  },
  // observer 包装: 渲染进程传 handlers 对象 (含 onToken/onToolCall 等), 主进程包装成
  // ISessionObserver 后调 session.addObserver. 返回 handle 供后续 remove.
  sessionAddObserver: (handlers) => {
    if (!nativeSession) return { ok: false, error: 'no session' };
    try {
      const jsOb = new JsSessionObserver(handlers || {});
      const nativeOb = avox.createJsSessionObserverOb(jsOb);
      nativeSession.addObserver(nativeOb);
      const handle = ++observerHandleCounter;
      observerHandles.set(handle, nativeOb);
      return { ok: true, handle };
    } catch (e) {
      return { ok: false, error: String(e && e.message || e) };
    }
  },
  sessionRemoveObserver: (handle) => {
    if (!nativeSession) return { ok: false, error: 'no session' };
    const nativeOb = observerHandles.get(handle);
    if (!nativeOb) return { ok: false, error: 'handle not found' };
    try { nativeSession.removeObserver(nativeOb); } catch (_) {}
    observerHandles.delete(handle);
    return { ok: true };
  },
  sessionEventCount: () => {
    if (!nativeSession) return 0;
    try { return nativeSession.eventCount() | 0; } catch (_) { return 0; }
  },
  sessionEventJson: (seq) => {
    if (!nativeSession) return null;
    try { return nativeSession.eventJson(seq | 0); } catch (_) { return null; }
  },
  sessionTrackPath: () => {
    if (!nativeSession) return '';
    try { return nativeSession.trackPath() || ''; } catch (_) { return ''; }
  },
  // 一次取最近 n 条事件 JSON (主进程串行拷出, 渲染进程解)
  sessionSnapshotRecent: (n) => {
    if (!nativeSession) return [];
    const total = nativeSession.eventCount() | 0;
    const from = Math.max(0, total - (n | 0));
    const out = [];
    for (let i = from; i < total; i++) {
      const line = nativeSession.eventJson(i);
      if (line == null) continue;
      try { out.push(JSON.parse(line)); }
      catch (_) { out.push({ seq: i, raw: String(line) }); }
    }
    return out;
  },
  // ---- 顶层 (无 host 也可单跑) ----
  runSkill: (name, input) => {
    try {
      return avox.runSkill(name || '', input == null ? '' : String(input));
    } catch (e) {
      return JSON.stringify({ skill: name, error: String(e && e.message || e) });
    }
  },
  // 列出 config/providers.json 里所有 chat=true 模型条目 (等价 AgentShell /list)。
  // 返回 JSON 字符串 (内部静态缓冲, 下次调用即失效), 这里直接传字符串给渲染进程。
  listProviders: () => {
    try {
      const raw = avox.avoxListProviders();
      if (!raw) return '[]';
      return typeof raw === 'string' ? raw : String(raw);
    } catch (e) {
      console.error('avoxListProviders error:', e);
      return '[]';
    }
  },
  // 诊断: 主进程推入的诊断行 (observer 回调触发情况), 渲染进程定期拉走贴到 eventLog.
  drainAgentDiag: () => {
    if (agentDiag.length === 0) return [];
    const out = agentDiag.splice(0, agentDiag.length);
    return out;
  },
  // ===== providers.json 编辑 =====
  // 读全量 JSON (user-level 优先, 找不到再回退 assets). 返回 {ok, json, path, source}.
  loadProvidersJson: () => readProvidersJsonAny(),
  // 写 user-level providers.json. C++ AssetLoader 优先读这个目录, 写完自动生效.
  saveProvidersJson: (json) => {
    try {
      if (typeof json !== 'string') {
        return { ok: false, error: 'json 必须是字符串' };
      }
      const p = userConfigPath('providers.json');
      fs.writeFileSync(p, json, 'utf8');
      return { ok: true, path: p };
    } catch (e) {
      return { ok: false, error: String(e && e.message || e) };
    }
  },
  // 取得 user-level 文件路径 (用于 UI 显示 "保存到 …").
  userProvidersJsonPath: () => userConfigPath('providers.json'),
});

window.addEventListener('message', (event) => {
  if (!event.data) return;
  if (event.data.type === 'TRANSFER_CANVAS') {
    const { offscreen, playerId } = event.data;
    const player = playerInstances.get(playerId);
    if (!player) {
      console.error(`未找到 playerId 为 ${playerId} 的播放器实例`);
      return;
    }
    console.log(`[${playerId}] 开始初始化 Offscreen 渲染器`);
    try {
      player.setOffscreen(offscreen);
      console.log(`[${playerId}] Offscreen 渲染隧道建立成功`);
    } catch (err) {
      console.error(`[${playerId}] 渲染初始化失败:`, err);
    }
  }
});

let currentHandle = null;
ipcRenderer.on('new-frame-handle', (event, handle) => {
  console.log(`Received new frame handle: ${handle}`);
  currentHandle = handle; // This is just a pointer/ID
});

console.log('preload.js loaded, avox api exposed to window');
