class BasePlayer {
  constructor(canvas) {
    this.canvas = canvas;
    this.jsObservers = {};
    // zoom/pan 状态
    this.zoom = 1.0;
    this.panX = 0.0;
    this.panY = 0.0;
    this.isDragging = false;
    this.lastMouseX = 0;
    this.lastMouseY = 0;
    this.createNativePlayer();
    this.checkPlayerObj();
    this.bindZoomEvents();
    this.bindResizeObserver();
  }
  createNativePlayer() {
    throw new Error('createNativePlayer must be implemented by subclass');
  }
  checkPlayerObj() {
    // 转移控制权，此后渲染进程无法再操作此 canvas
    // contextBridge传过去是个空对象
    const offscreen = this.canvas.transferControlToOffscreen();
    // 用postMessage是因为offscreen只能用postMessage传递
    window.postMessage({
      type: 'TRANSFER_CANVAS',
      offscreen: offscreen,
      playerId: this.playerId
    }, '*', [offscreen]);
  }
  clampPan() {
    const maxPan = (this.zoom - 1) / this.zoom;
    this.panX = Math.max(-maxPan, Math.min(maxPan, this.panX));
    this.panY = Math.max(-maxPan, Math.min(maxPan, this.panY));
  }
  notifyZoomPan() {
    if (this.nplayer && this.nplayer.setZoomPan) {
      this.nplayer.setZoomPan(this.zoom, this.panX, this.panY);
    }
  }
  notifyResize(width, height) {
    if (this.nplayer && this.nplayer.resizeCanvas) {
      this.nplayer.resizeCanvas(width, height);
    }
  }
  bindZoomEvents() {
    this.canvas.addEventListener('wheel', (e) => {
      e.preventDefault();
      e.stopPropagation();
      const delta = e.deltaY > 0 ? 0.9 : 1.1;
      const oldZoom = this.zoom;
      this.zoom = Math.max(1, Math.min(5, this.zoom * delta));
      if (this.zoom !== oldZoom) {
        const rect = this.canvas.getBoundingClientRect();
        const mouseX = (e.clientX - rect.left) / rect.width;
        const mouseY = (e.clientY - rect.top) / rect.height;
        const zoomRatio = this.zoom / oldZoom;
        this.panX = mouseX - (mouseX - this.panX) / zoomRatio;
        this.panY = mouseY - (mouseY - this.panY) / zoomRatio;
        this.clampPan();
        this.notifyZoomPan();
      }
    }, { passive: false, capture: true });

    this.canvas.addEventListener('mousedown', (e) => {
      if (this.zoom > 1) {
        e.preventDefault();
        e.stopPropagation();
        this.isDragging = true;
        this.lastMouseX = e.clientX;
        this.lastMouseY = e.clientY;
        this.canvas.style.cursor = 'grabbing';
      }
    }, { capture: true });

    this.canvas.addEventListener('mousemove', (e) => {
      if (!this.isDragging) return;
      e.preventDefault();
      e.stopPropagation();
      const rect = this.canvas.getBoundingClientRect();
      // 鼠标移出 canvas 立即停止拖拽
      if (e.clientX < rect.left || e.clientX > rect.right ||
        e.clientY < rect.top || e.clientY > rect.bottom) {
        this.isDragging = false;
        this.canvas.style.cursor = this.zoom > 1 ? 'grab' : 'default';
        return;
      }
      const dx = (e.clientX - this.lastMouseX) / rect.width;
      const dy = (e.clientY - this.lastMouseY) / rect.height;
      this.panX -= dx / this.zoom;
      this.panY -= dy / this.zoom;
      this.clampPan();
      this.lastMouseX = e.clientX;
      this.lastMouseY = e.clientY;
      this.notifyZoomPan();
    }, { capture: true });

    const endDrag = (e) => {
      if (this.isDragging) {
        e.preventDefault();
        e.stopPropagation();
      }
      this.isDragging = false;
      this.canvas.style.cursor = this.zoom > 1 ? 'grab' : 'default';
    };
    this.canvas.addEventListener('mouseup', endDrag, { capture: true });
    this.canvas.addEventListener('mouseleave', endDrag, { capture: true });

  }
  bindResizeObserver() {
    this.resizeObserver = new ResizeObserver((entries) => {
      for (const entry of entries) {
        const { width, height } = entry.contentRect;
        if (width > 0 && height > 0) {
          const dpr = window.devicePixelRatio || 1;
          const physWidth = Math.round(width * dpr);
          const physHeight = Math.round(height * dpr);
          // debounce: 只在rAF时发送resize,避免拖拽时高频触发
          this.pendingResizeSize = { width: physWidth, height: physHeight };
          if (!this.resizeRafPending) {
            this.resizeRafPending = true;
            requestAnimationFrame(() => {
              this.resizeRafPending = false;
              if (this.pendingResizeSize) {
                const { width, height } = this.pendingResizeSize;
                this.pendingResizeSize = null;
                this.notifyResize(width, height);
              }
            });
          }
        }
      }
    });
    this.resizeObserver.observe(this.canvas);
  }
  destroy() {
    if (this.resizeObserver) {
      this.resizeObserver.disconnect();
      this.resizeObserver = null;
    }
  }
  onStateChange(callback) {
    this.nplayer.on('onStateChange', callback);
  }
  onIoError(callback) {
    this.nplayer.on('onIoError', callback);
  }
  onDecodeError(callback) {
    this.nplayer.on('onDecodeError', callback);
  }
  onReady(callback) {
    this.nplayer.on('onReady', callback);
  }
  onComplete(callback) {
    this.nplayer.on('onComplete', callback);
  }
  onSeek(callback) {
    this.nplayer.on('onSeek', callback);
  }
  onPause(callback) {
    this.nplayer.on('onPause', callback);
  }
  onResume(callback) {
    this.nplayer.on('onResume', callback);
  }
  onClose(callback) {
    this.nplayer.on('onClose', callback);
  }
}

class MediaPlayer extends BasePlayer {
  constructor(canvas) {
    super(canvas);
  }
  // 计算解码策略：前16个软解，之后硬解
  static getDecodeStrategy() {
    const count = window.avox.getPlayerCount();
    return { hardDecode: count >= 16 };
  }
  getPlayerIndex() {
    return window.avox.getPlayerIndex(this.playerId);
  }
  static getPlayerCount() {
    return window.avox.getPlayerCount();
  }
  createNativePlayer() {
    this.nplayer = window.avox.createMediaPlayer();
    this.playerId = this.nplayer.playerId;
    // 动态委托：将 nplayer 的方法挂载到 this 上
    Object.keys(this.nplayer).forEach(prop => {
      if (prop !== 'constructor' && typeof this.nplayer[prop] === 'function') {
        this[prop] = (...args) => this.nplayer[prop](...args);
      }
    }); 
    // 如果用到图像修改,包含添加字幕,混合,缩放都需要用vulkan    
    this.nplayer.setVulkan(true); 
    // electron对接C++需要极限性能的的话
    // 用软解+不启用vulkan,这样软解的YUV420直接放到网页上
    // this.nplayer.setVulkan(false); 
    // 非vulkan一定要硬解
    this.nplayer.setHardDecode(false);       
    // this.nplayer.getOption().setBool("log.source.packet", true);
    this.nplayer.getOption().setInt("mp.delay.ms", 2000);
    // this.nplayer.getOption().setInt("mp.synctype", 2);
    // 注册状态变化回调
    this.nplayer.on('onStateChange', (preState, state) => {
      this.onStateChangeInternal(state);
    });
  }
  // 内部状态变化处理
  onStateChangeInternal(state) {
  }
  // 获取播放位置(毫秒)，对应正在渲染的PTS
  getPosition() {
    return this.nplayer.getPosition();
  }
  // 获取媒体源总时间(毫秒)，<=0 表示直播源不确定结束时间
  getDuration() {
    return this.nplayer.getDuration();
  }
  // 获取开始时间(毫秒)，注意如果有跳变的PTS，可能从跳变的PTS开始
  getStartTime() {
    return this.nplayer.getStartTime();
  }
  startRecord(path,bTranscode) {
    this.muxer = this.nplayer.getMuxer(bTranscode);
    // MuxerType_ffmpeg
    this.muxer.setMuxerType(avox.MuxerType_zlmediakit);
    if (!this.muxer) {
      console.log('播放器未初始化', 'error');
      return;
    }
    this.muxer.open(path);
    console.log(`开始录像: ${path}`, 'success');
  }
  stopRecord() {
    if (this.muxer) {
      this.muxer.close();
      this.muxer = null;
      console.log('录像已停止', 'success');
    }
  }
  destroy() {
    super.destroy();
    if (this.nplayer) {
      this.nplayer.destroy();
      this.nplayer = null;
    }
  }
}

class WebRtcPlayer extends BasePlayer {
  constructor(canvas) {
    super(canvas);
  }
  createNativePlayer() {
    // 创建原生播放器实例
    this.nplayer = window.avox.createWebRtcPlayer();
    this.playerId = this.nplayer.playerId;
    // 动态委托：将 nplayer 的方法挂载到 this 上
    Object.keys(this.nplayer).forEach(prop => {
      if (prop !== 'constructor' && typeof this.nplayer[prop] === 'function') {
        this[prop] = (...args) => this.nplayer[prop](...args);
      }
    });
  }
  setRollType(answer) {
    this.nplayer.setRollType(answer);
  }
  addIceCandidate(candidate, sdpMid, sdpMLineIndex) {
    this.nplayer.addIceCandidate(candidate, sdpMid, sdpMLineIndex);
  }
  onLocalSdp(callback) {
    this.nplayer.on('onLocalSdp', callback);
  }
  onIceCandidate(callback) {
    this.nplayer.on('onIceCandidate', callback);
  }
}

class SourcePlayer {
  constructor() {
    this.nplayer = window.avox.createSourcePlayer();
    this.playerId = this.nplayer.playerId;
    if (!this.nplayer) {
      console.error('Failed to create SourcePlayer');
      this.nplayer = null;
      return;
    }
  }
  // 音频推流：直接转发到主进程，配置由主进程的 SourcePlayer.startPush 处理
  startPush(url, muxerConfig) {
    this.nplayer.startPush(url, muxerConfig);
  }
  stopPush() {
    this.nplayer.stopPush();
  }
  getAudioDeviceCount() {
    return this.nplayer.getAudioDeviceCount();
  }
  getAudioDeviceName(index) {
    return this.nplayer.getAudioDeviceName(index);
  }
  getMuxer() {
    return this.nplayer.getMuxer();
  }
  destroy() {
    if (this.nplayer) {
      this.nplayer.destroy();
      this.nplayer = null;
    }
  }
}

// ========== Recorder 封装类（流录制器）==========
class Recorder {
  constructor(bTranscode = false) {
    // 转码录制需要Vulkan支持，不可用时降级为直接录制
    if (bTranscode && !window.avox.canVulkan()) {
      bTranscode = false;
      console.warn('转码录制需要Vulkan支持，当前环境不可用，已降级为直接录制');
    }
    this.bTranscode = bTranscode;
    this.jsObservers = {};
    this.nrecorder = window.avox.createRecorder(bTranscode);
    this.recorderId = this.nrecorder.recorderId;
    // 转码模式下获取 SurfaceRender 用于图像处理
    if (bTranscode) {
      this.surfaceRender = this.nrecorder.getSurfaceRender();
    }
  }
  onIoError(callback) {
    this.nrecorder.on('onIoError', callback);
  }
  onProgress(callback) {
    this.nrecorder.on('onProgress', callback);
  }
  onComplete(callback) {
    this.nrecorder.on('onComplete', callback);
  }
  setIoPlan(plan) {
    this.nrecorder.setIoPlan(plan);
  }
  setMuxerType(type) {
    this.nrecorder.setMuxerType(type);
  }
  setVideoCodec(codecId) {
    this.nrecorder.setVideoCodec(codecId);
  }
  setAudioCodec(codecId) {
    this.nrecorder.setAudioCodec(codecId);
  }
  open(inputUrl, outputFile) {
    return this.nrecorder.open(inputUrl, outputFile);
  }
  close() {
    if (this.nrecorder) {
      this.nrecorder.close();
    }
  }
  getState() {
    return this.nrecorder.getState();
  }
  getSurfaceRender() {
    if (!this.bTranscode) {
      console.warn('Recorder: getSurfaceRender only available in transcode mode');
      return null;
    }
    return this.surfaceRender;
  }
  destroy() {
    if (this.nrecorder) {
      this.nrecorder.destroy();
      this.nrecorder = null;
    }
  }
}

// ========== 导出模块 ==========
// Node.js 模块导出（在渲染进程不生效）
if (typeof module !== 'undefined' && module.exports) {
  module.exports = {
    BasePlayer,
    MediaPlayer,
    WebRtcPlayer,
    SourcePlayer,
    Recorder,
  };
}

// 浏览器环境：自动成为全局变量（通过 script 标签加载时）
// 使用 window 访问：window.MediaPlayer, window.WebRtcPlayer, window.PlayerState

// 页面加载时执行 WebGL 支持检测
if (typeof document !== 'undefined' && document.readyState === 'loading') {
  document.addEventListener('DOMContentLoaded', () => {
    console.log('[avox_player.js] Page loaded, running WebGL detection...');
  });
} else if (typeof document !== 'undefined') {
  // DOM 已经加载完成
  console.log('[avox_player.js] DOM already loaded, running WebGL detection...');
}

// 是否打印C++层日志
// window.avox.nativeLog(true, (level, msg) => {
//   console.log(`[avox] [${level}] ${msg}`);
// });
