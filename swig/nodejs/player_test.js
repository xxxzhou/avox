// AVOX 播放器测试套件 (适用于渲染进程)
class PlayerTestSuite {
  constructor() {
    // 依赖关系通过 window.avox 获取
    this.api = window.avox;
    // 播放器状态
    this.players = [];
    this.playerType = 'media';
    this.frameCount = 0;
    this.videoResolution = '0x0';
    // 播放时长定时器
    this.playDurationTimer = null;
    // 录制器
    this.recorder = null;
    // 语音对讲播放器
    this.talkPlayer = null;
    // ONVIF 检测结果
    this.onvifResult = null;
    // 字体显示层
    this.fontLayer = null;
    // 全屏放大状态: 双击某个播放器放大到全屏
    this.fullscreenIndex = -1;
    // 执行初始化
    this.init();
  }

  init() {
    if (!this.api) {
      this.log('not load acv,check preload.js', 'error');
      return;
    }
    console.log('avox api 已加载', this.api);
    this.setupEventListeners();
    this.createPlayers();
  }

  setupEventListeners() {
    // 播放器类型选择器
    document.querySelectorAll('input[name="playerType"]').forEach(radio => {
      radio.addEventListener('change', (e) => {
        this.switchPlayerType(e.target.value);
      });
    });

    // 通用控制 - 所有播放器一起操作
    document.getElementById('btnPlay').addEventListener('click', () => {
      const url = document.getElementById('urlInput').value;
      this.players.forEach(p => {
        p.open(url);
      });
    });

    document.getElementById('btnPause').addEventListener('click', () => this.players.forEach(p => p.pause()));
    document.getElementById('btnResume').addEventListener('click', () => this.players.forEach(p => p.resume()));
    document.getElementById('btnStop').addEventListener('click', () => this.players.forEach(p => p.close()));

    // 点播 Seek 控制 (±10s)
    document.getElementById('btnSeekBack').addEventListener('click', () => this.seekBy(-10000));
    document.getElementById('btnSeekForward').addEventListener('click', () => this.seekBy(10000));

    // 速度控制
    const speedButtons = [
      { id: 'btnSpeed05', value: 0.5 },
      { id: 'btnSpeed1',  value: 1 },
      { id: 'btnSpeed2',  value: 2 },
      { id: 'btnSpeed4',  value: 4 },
      { id: 'btnSpeed8',  value: 8 },
      { id: 'btnSpeed16', value: 16 },
    ];
    speedButtons.forEach(({ id, value }) => {
      document.getElementById(id).addEventListener('click', () => {
        this.players.forEach(p => p.speed(value));
        // 更新按钮高亮
        speedButtons.forEach(s => document.getElementById(s.id).classList.remove('speed-active'));
        document.getElementById(id).classList.add('speed-active');
        this.log(`Speed: ${value}x`, 'success');
      });
    });

    // 设置 - 所有播放器一起操作
    document.getElementById('btnHardDecodeOn').addEventListener('click', () => this.players.forEach(p => p.setHardDecode(true)));
    document.getElementById('btnHardDecodeOff').addEventListener('click', () => this.players.forEach(p => p.setHardDecode(false)));

    // IO 计划
    document.getElementById('btnIoPlanFFmpeg').addEventListener('click', () => this.players.forEach(p => p.setIoPlan(this.api.IoPlan_ffmpeg)));
    document.getElementById('btnIoPlanZlMediaKit').addEventListener('click', () => this.players.forEach(p => p.setIoPlan(this.api.IoPlan_zlmediakit)));

    // 效果 - 所有播放器一起操作，转码 Recorder 同步应用
    document.getElementById('btnWatermarkOn').addEventListener('click', () => {
      let watermark = {};
      watermark.centerX = 0.3;
      watermark.centerY = 0.3;
      watermark.width = 0.3;
      watermark.height = 0.3;
      watermark.alaph = 0.3;
      // 对应avox下assets/images里的图像
      // 如果外部路径,重写preload.js里的enableWatermark
      // 使用avox.loadImagePath替换loadImageAsset
      this.players.forEach(p => p.getSurfaceRender().enableWatermark(watermark,"blend.png"));
      // 转码 Recorder 同步应用水印
      const recorderRender = this.recorder?.getSurfaceRender();
      if (recorderRender) recorderRender.enableWatermark(watermark, "blend.png");
    });
    document.getElementById('btnWatermarkOff').addEventListener('click', () => {
      this.players.forEach(p => p.getSurfaceRender().disableWatermark());
      const recorderRender = this.recorder?.getSurfaceRender();
      if (recorderRender) recorderRender.disableWatermark();
    });
    document.getElementById('btnLutOn').addEventListener('click', () => {
      this.players.forEach(p => p.getSurfaceRender().enableLut(1));
      const recorderRender = this.recorder?.getSurfaceRender();
      if (recorderRender) recorderRender.enableLut(1);
    });
    document.getElementById('btnLutOff').addEventListener('click', () => {
      this.players.forEach(p => p.getSurfaceRender().disableLut());
      const recorderRender = this.recorder?.getSurfaceRender();
      if (recorderRender) recorderRender.disableLut();
    });

    // 字体控制
    document.getElementById('btnFontOn').addEventListener('click', () => this.enableFont());
    document.getElementById('btnFontOff').addEventListener('click', () => this.disableFont());

    // Size Scale 控制
    document.getElementById('btnScale025').addEventListener('click', () => {
      this.players.forEach(p => p.getSurfaceRender().enableSizeScale(0.25));
      this.log('Size Scale: 0.25x', 'success');
    });
    document.getElementById('btnScale05').addEventListener('click', () => {
      this.players.forEach(p => p.getSurfaceRender().enableSizeScale(0.5));
      this.log('Size Scale: 0.5x', 'success');
    });
    document.getElementById('btnScale075').addEventListener('click', () => {
      this.players.forEach(p => p.getSurfaceRender().enableSizeScale(0.75));
      this.log('Size Scale: 0.75x', 'success');
    });
    document.getElementById('btnScale1').addEventListener('click', () => {
      this.players.forEach(p => p.getSurfaceRender().enableSizeScale(1));
      this.log('Size Scale: 1x (original)', 'success');
    });
    document.getElementById('btnScale2').addEventListener('click', () => {
      this.players.forEach(p => p.getSurfaceRender().enableSizeScale(2));
      this.log('Size Scale: 2x', 'success');
    });
    document.getElementById('btnDisableSizeChange').addEventListener('click', () => {
      this.players.forEach(p => p.getSurfaceRender().disableSizeChange());
      this.log('Size Change: disabled', 'success');
    });
    document.getElementById('btnAutoSizeScale').addEventListener('click', () => {
      const player = this.players[0];
      if (!player || !player.getAutoSizeScale) return;
      const newState = !player.getAutoSizeScale();
      this.players.forEach(p => {
        if (p.setAutoSizeScale) p.setAutoSizeScale(newState);
      });
      this.log(`autoSizeScale: ${newState ? 'enabled' : 'disabled'}`, 'success');
    });

    // 截图按钮
    document.getElementById('btnScreenShot').addEventListener('click', () => {
      this.screenShot();
    });
    document.getElementById('btnScreenShotBase64').addEventListener('click', () => {
      this.screenShotBase64();
    });

    // 录像控制
    document.getElementById('btnRecordStart').addEventListener('click', () => this.startRecord());
    document.getElementById('btnRecordStop').addEventListener('click', () => this.stopRecord());

    // Recorder 控制
    document.getElementById('btnRecorderStart').addEventListener('click', () => this.startRecorder());
    document.getElementById('btnRecorderStop').addEventListener('click', () => this.stopRecorder());

    // 转码 checkbox 切换时，如果已有 recorder 在运行则阻止
    document.getElementById('recorderTranscode').addEventListener('change', (e) => {
      if (this.recorder) {
        this.log('请先停止当前录制再更改转码模式', 'error');
        e.target.checked = !e.target.checked;
      }
    });

    // 语音对讲控制
    document.getElementById('btnTalkStart').addEventListener('click', () => this.startTalk());
    document.getElementById('btnTalkStop').addEventListener('click', () => this.stopTalk());

    // ONVIF 检测
    document.getElementById('btnCheckOnvif').addEventListener('click', () => this.checkOnvifBackchannel());

    // WebRTC 控制
    document.getElementById('btnWebRtcStart').addEventListener('click', () => this.players.forEach(p => p.open()));
    document.getElementById('btnWebRtcStop').addEventListener('click', () => this.players.forEach(p => p.stop()));

    document.getElementById('btnSetRemoteSdp').addEventListener('click', () => {
      const sdp = document.getElementById('remoteSdp').value;
      if (sdp) this.players.forEach(p => p.setRemoteSdp(sdp));
    });

    document.getElementById('btnCopyLocalSdp').addEventListener('click', () => {
      const localSdp = document.getElementById('localSdp').value;
      if (localSdp) navigator.clipboard.writeText(localSdp);
    });

    // 进度条点击跳转
    document.getElementById('progressBar').addEventListener('click', (e) => {
      const player = this.players[0];
      if (!player) return;
      const duration = player.getDuration();
      if (duration <= 0) return; // 直播源不支持跳转
      const startTime = player.getStartTime();
      const relativeDur = Math.max(0, duration - startTime);
      const rect = e.currentTarget.getBoundingClientRect();
      const ratio = Math.max(0, Math.min(1, (e.clientX - rect.left) / rect.width));
      const seekTime = startTime + ratio * relativeDur;
      player.seek(seekTime);
    });
  }

  createPlayers() {
    // 旧播放器异步分批销毁(每路 nplayer.close 可能在等命令队列,串行会冻结主线程)
    const oldPlayers = this.players;
    this.players = [];
    this.destroyPlayersGradually(oldPlayers);
    // 清理播放时长定时器
    if (this.playDurationTimer) {
      clearInterval(this.playDurationTimer);
      this.playDurationTimer = null;
    }
    // 重置全屏放大状态
    this.fullscreenIndex = -1;

    // 获取当前网格中的所有 canvas
    const canvases = document.querySelectorAll('#videoGrid canvas');
    canvases.forEach((canvas, i) => {
      const player = this.createSinglePlayer(this.playerType, canvas, i);
      if (player) {
        this.players.push(player);
      }
    });

    // 为第一个播放器设置事件监听器（用于状态显示）
    if (this.players.length > 0) {
      this.setupPlayerEventListeners(this.players[0]);
    }
    // 更新 UI
    this.updateUIControls();
    // 多屏缩放: 播放器数>1时自动启用0.5x缩放
    this.updateMultiScreenScale();
    // 绑定双击放大/恢复事件
    this.bindDblClickFullscreen();
  }

  // 旧播放器分批销毁:每帧销毁 BATCH 路,把同步阻塞切片到多个帧,避免主线程冻结
  destroyPlayersGradually(players) {
    if (!players || players.length === 0) return;
    const BATCH = 2;            // 每帧销毁数量,按机器调整
    let i = 0;
    const step = () => {
      for (let k = 0; k < BATCH && i < players.length; k++, i++) {
        try { players[i].destroy(); } catch (e) { console.error('destroy error:', e); }
      }
      if (i < players.length) requestAnimationFrame(step);
    };
    step();
  }

  createSinglePlayer(type, canvas, index) {
    let player;
    if (type === 'media') {
      player = new MediaPlayer(canvas);
    } else {
      player = new WebRtcPlayer(canvas);
      player.on('onLocalSdp', async (localSdp) => {
        const url = document.getElementById('urlInput').value;
        try {
          const response = await fetch(url, {
            method: 'POST',
            headers: {
              'Content-Type': 'text/plain;charset=utf-8',
              'User-Agent': 'Mozilla/5.0',
              'Accept': '*/*'
            },
            body: localSdp
          });

          if (!response.ok) {
            throw new Error(`HTTP error! status: ${response.status}`);
          }
          const content = await response.json();
          if (content.code !== 0) {
            console.warn(`zlmediakit response code: ${content.code}, msg: ${content.msg}`);
            return;
          }
          const remoteSdp = content.sdp;
          console.log('zlmediakit response sdp:', sdp);
          if (player) {
            player.setRemoteSdp(remoteSdp);
          }
        } catch (error) {
          console.error('zlmediakit request failed:', error.message);
        }
      });
    }
    if (!player) {
      this.log(`错误：创建播放器 ${index} 失败`, 'error');
    }
    return player;
  }

  setupPlayerEventListeners(player) {
    console.log('[setupPlayerEventListeners] Setting up event listeners, player:', player);
    this.frameCount = 0;
    player.onStateChange((preState, state) => {
      document.getElementById('playerState').textContent = state;
      console.log(`player: ${preState} -> ${state}`);
      this.updatePlayDurationDisplay(state);
    })
    player.onSeek(() => {
      // 跳转完成后立即更新时间显示
      this.updateTimeDisplay();
    })
    player.onIoError((code, msg) => {
      console.error(`player error: code=${code}, msg=${msg}`);
    });
    player.onReady(() => {
      console.log('player ready');
      const sourceInfo = player.getSourceInfo();
      let videoInfo = '-';
      let audioInfo = '-';
      if (sourceInfo && sourceInfo.videoSize() > 0) {
        const vtrackDesc = sourceInfo.getVideoDesc(0);
        const vcodename = window.avox.getVCodecName(vtrackDesc.codecId);
        const yuvTypeStr = window.avox.getYuvTypeStr(vtrackDesc.desc.type);
        // 格式: codec-width*height@fps-yuv (如: h264-1920*1080@30.0-nv12)
        videoInfo = `${vcodename}-${vtrackDesc.desc.width}*${vtrackDesc.desc.height}@${vtrackDesc.desc.fps.toFixed(1)}-${yuvTypeStr}`;
        console.log(`jsob video: ${videoInfo}`);
      }
      if (sourceInfo && sourceInfo.audioSize() > 0) {
        const atrackDesc = sourceInfo.getAudioDesc(0);
        const acodename = window.avox.getACodecName(atrackDesc.codecId);
        const formatStr = window.avox.getAudioFormatStr(atrackDesc.desc.format);
        // 格式: codec-channels/sampleRate/format (如: aac-2/44100/fltp)
        audioInfo = `${acodename}-${atrackDesc.desc.channels}/${atrackDesc.desc.sampleRate}/${formatStr}`;
        console.log(`jsob audio: ${audioInfo}`);
      }
      // 更新界面显示
      document.getElementById('videoInfo').textContent = videoInfo;
      document.getElementById('audioInfo').textContent = audioInfo;
    });
  }

  // 异步截图
  screenShot() {
    const path = document.getElementById('screenShotPath').value;
    if (!path) {
      this.log('请输入截图保存路径', 'error');
      return;
    }
    const player = this.players[0];
    const render = player?.getSurfaceRender();
    if (!render) {
      this.log('播放器未初始化', 'error');
      return;
    }
    Promise.resolve().then(() => {
      return render.screenShotToPath(path);
    }).then((result) => {
      this.log(result ? `截图已保存: ${path}` : '截图失败', result ? 'success' : 'error');
    });
  }

  // 截图转 base64
  screenShotBase64() {
    const player = this.players[0];
    const render = player?.getSurfaceRender();
    if (!render) {
      this.log('播放器未初始化', 'error');
      return;
    }
    Promise.resolve().then(() => {
      return render.screenShotToBase64();
    }).then((b64) => {
      if (b64) {
        this.log(`base64 (length=${b64.length}): ${b64.substring(0, 100)}...`, 'success');
      } else {
        this.log('截图base64失败', 'error');
      }
    });
  }

  // 开始录像
  startRecord() {
    const path = document.getElementById('recordPath').value;
    if (!path) {
      this.log('请输入录像保存路径', 'error');
      return;
    }
    const bTranscode = document.getElementById('recordTranscode').checked;
    const player = this.players[0];
    player.startRecord(path, bTranscode);
  }

  // 停止录像
  stopRecord() {
    const player = this.players[0];
    player.stopRecord();
  }

  // 启用字体显示
  enableFont() {
    const player = this.players[0];
    const render = player?.getSurfaceRender();
    if (!render) { this.log('播放器未初始化', 'error'); return; }
    this.fontLayer = render.enableFont();
    if (!this.fontLayer) { this.log('字体功能不可用(需要Vulkan后端)', 'error'); return; }
    // 固定默认：白色，左上角，缩放1.5
    this.fontLayer.setColor(1, 1, 1, 0);
    this.fontLayer.updateLayout(0, { x: 0.05, y: 0.05, width: 0.5, height: 0.2, horizontal: 1, vertical: 1 });
    // 左下角
    // this.fontLayer.setFontLayout({ x: 0.05, y: 0.95, width: 0.8, height: 0.4, horizontal: 1, vertical: 2 });
    this.fontLayer.setScale(1.5);
    this.fontLayer.drawText('AVOX Player 文字测试');
    // 转码 Recorder 同步启用字体
    const recorderRender = this.recorder?.getSurfaceRender();
    if (recorderRender) {
      this.recorderFontLayer = recorderRender.enableFont();
      if (this.recorderFontLayer) {
        this.recorderFontLayer.setColor(1, 1, 1, 0);
        this.recorderFontLayer.updateLayout(0, { x: 0.05, y: 0.05, width: 0.5, height: 0.2, horizontal: 1, vertical: 1 });
        this.recorderFontLayer.setScale(1.5);
        this.recorderFontLayer.drawText('Recorder 文字测试');
      }
    }
    this.log('字体显示已启用', 'success');
  }

  // 关闭字体显示
  disableFont() {
    const player = this.players[0];
    const render = player?.getSurfaceRender();
    if (!render) return;
    render.disableFont();
    this.fontLayer = null;
    // 转码 Recorder 同步关闭字体
    const recorderRender = this.recorder?.getSurfaceRender();
    if (recorderRender) {
      recorderRender.disableFont();
      this.recorderFontLayer = null;
    }
    this.log('字体显示已关闭', 'success');
  }

  // 开始流录制
  startRecorder() {
    const inputUrl = document.getElementById('recorderInput').value;
    const outputPath = document.getElementById('recorderOutput').value;
    if (!inputUrl || !outputPath) {
      this.log('请输入录制输入地址和输出路径', 'error');
      return;
    }
    if (this.recorder) {
      this.recorder.destroy();
      this.recorder = null;
    }
    // 用户设置的录制时长(秒), 0=不限
    this.recorderDurationSec = parseInt(document.getElementById('recorderDuration').value) || 0;
    // 清除上次进度显示
    const progressEl = document.getElementById('recorderProgress');
    if (progressEl) progressEl.textContent = '';
    let bTranscode = document.getElementById('recorderTranscode').checked;
    if (bTranscode && !window.avox.canVulkan()) {
      bTranscode = false;
      console.warn('转码录制需要Vulkan支持，当前环境不可用，已降级为直接录制');
    }
    this.recorder = new Recorder(bTranscode);
    const rid = this.recorder.recorderId;
    this.recorder.onProgress((currentTimeMs, totalTimeMs) => {
      const curStr = this.formatTime(currentTimeMs);
      const totalStr = totalTimeMs > 0 ? this.formatTime(totalTimeMs) : 'LIVE';
      // 用户设置了时长限制, 取用户限制与源总时长中较小值
      const limitMs = this.recorderDurationSec > 0 ? Math.min(this.recorderDurationSec * 1000, totalTimeMs > 0 ? totalTimeMs : Infinity) : 0;
      const percent = limitMs > 0 ? Math.min(100, (currentTimeMs / limitMs * 100)).toFixed(1) : (totalTimeMs > 0 ? Math.min(100, (currentTimeMs / totalTimeMs * 100)).toFixed(1) : '--');
      // 更新录制时长旁的进度显示
      const progressEl = document.getElementById('recorderProgress');
      if (progressEl) {
        progressEl.textContent = `${curStr} / ${totalStr} (${percent}%)`;
      }
      if (this.recorder && limitMs > 0 && currentTimeMs >= limitMs) {
        this.log(`[Recorder#${rid}] 到达录制时长限制 (${(limitMs / 1000).toFixed(1)}s)，自动停止`, 'success');
        this.stopRecorder();
      }
    });
    this.recorder.onIoError((error, msg) => {
      this.log(`[Recorder#${rid}] 录制错误: ${error} - ${msg}`, 'error');
      const progressEl = document.getElementById('recorderProgress');
      if (progressEl) progressEl.textContent = '';
    });
    this.recorder.onComplete(() => {
      this.log(`[Recorder#${rid}] 录制完成`, 'success');
      const progressEl = document.getElementById('recorderProgress');
      if (progressEl) progressEl.textContent = '';
    });
    // 按URL协议自动选IO与速度: rtsp→zlmediakit+4x(点播/回放源全帧率上限,8x起服务端抽帧),
    // http/hls等HTTP系→ffmpeg原速(HLS本身即全速分片下载)
    const isRtsp = /^rtsp:\/\//i.test(inputUrl);
    this.recorder.setIoPlan(isRtsp ? this.api.IoPlan_zlmediakit : this.api.IoPlan_ffmpeg);
    if (isRtsp) {
      this.recorder.nrecorder.setSpeed(4);
    }
    const result = this.recorder.open(inputUrl, outputPath);
    if (result) {
      const durationHint = this.recorderDurationSec > 0 ? `，限时 ${this.recorderDurationSec}s` : '';
      this.log(`[Recorder#${rid}] 开始录制: ${inputUrl} -> ${outputPath} (转码: ${bTranscode}${durationHint})`, 'success');
    } else {
      this.log(`[Recorder#${rid}] 录制启动失败`, 'error');
    }
  }

  // 停止流录制
  stopRecorder() {
    if (!this.recorder) {
      this.log('录制器未初始化', 'error');
      return;
    }
    const rid = this.recorder.recorderId;
    this.recorder.close();
    this.recorder.destroy();
    this.recorder = null;
    const progressEl = document.getElementById('recorderProgress');
    if (progressEl) progressEl.textContent = '';
    this.log(`[Recorder#${rid}] 录制已停止`, 'success');
  }

  // 开始语音对讲
  startTalk() {
    const url = document.getElementById('talkPushUrl').value;
    if (!url) {
      this.log('请输入对讲推流地址', 'error');
      return;
    }
    // 获取推流模式
    const talkModeRadio = document.querySelector('input[name="talkMode"]:checked');
    if (!talkModeRadio) {
      this.log('请选择对讲模式', 'error');
      return;
    }
    const isOnvif = talkModeRadio.value === 'onvif';

    // ONVIF 模式需要先检测
    if (isOnvif && !this.onvifResult) {
      this.log('请先检测 ONVIF Backchannel', 'error');
      return;
    }

    this.talkPlayer = this.api.createSourcePlayer('talk-player');
    if (!this.talkPlayer) {
      this.log('创建对讲播放器失败', 'error');
      return;
    }
    const muxer = this.talkPlayer.getMuxer();
    muxer.on('onIoError', (error, msg) => {
      this.log(`推流错误: ${error} - ${msg}`, 'error');
    });
    muxer.on('onStateChange', (preState, state) => {
      this.log(`推流状态: ${preState} -> ${state}`);
    });
    const deviceCount = this.talkPlayer.getAudioDeviceCount();
    this.log(`检测到 ${deviceCount} 个音频设备`);
    for (let i = 0; i < deviceCount; i++) {
      this.log(`  [${i}] ${this.talkPlayer.getAudioDeviceName(i)}`);
    }
    // ACodecId::g711a
    let codecId = 1;
    let sampleRate = 8000;
    let channels = 1;
    // ONVIF Backchannel 模式
    if (isOnvif) {
      codecId = this.onvifResult.audioDesc.codecId || 1;
      sampleRate = this.onvifResult.audioDesc.sampleRate || 8000;
      channels = this.onvifResult.audioDesc.channels || 1;
      const codecNames = { 0: 'AAC', 1: 'G.711A', 2: 'G.711U', 3: 'opus' };
      this.log(`ONVIF Backchannel 模式: ${codecNames[codecId] || codecId}, ${sampleRate}Hz, ${channels}ch`);
      this.talkPlayer.startPush(url, {
        hardEncode: true,
        muxerType: this.api.MuxerType_onvif,
        audioCodecId: codecId,
        sampleRate: sampleRate,
        channels: channels
      });
    } else {
      // 普通推流模式
      this.talkPlayer.startPush(url, {
        hardEncode: true,
        muxerType: this.api.MuxerType_zlmediakit,
        audioCodecId: codecId,
        sampleRate: sampleRate,
        channels: channels
      });
    }
    this.log(`开始语音对讲: ${url} (${isOnvif ? 'ONVIF Backchannel' : '普通推流'})`, 'success');
  }

  // 检测 ONVIF Backchannel
  checkOnvifBackchannel() {
    const url = document.getElementById('talkPushUrl').value;
    if (!url) {
      this.log('请输入对讲推流地址', 'error');
      return;
    }
    const resultSpan = document.getElementById('onvifCheckResult');
    resultSpan.textContent = '检测中...';
    resultSpan.style.color = '#888';

    try {
      // 注意：checkOnvif 会消耗一次 token，检测后需要使用新的 URL
      const result = this.api.checkOnvif(url);
      this.onvifResult = result;
      const audioInfo = document.getElementById('onvifAudioInfo');
      if (result.isOnvifBackchannel) {
        const codecNames = { 0: 'AAC', 1: 'G.711A', 2: 'G.711U', 3: 'opus' };
        const codecName = codecNames[result.audioDesc.codecId] || result.audioDesc.codecId;
        resultSpan.textContent = '✓ 支持 ONVIF Backchannel';
        resultSpan.style.color = '#51cf66';
        audioInfo.textContent = `${codecName} ${result.audioDesc.sampleRate}Hz ${result.audioDesc.channels}ch`;
        this.log(`ONVIF Backchannel 检测成功: 编码=${codecName}, 采样率=${result.audioDesc.sampleRate}Hz`);
        // 自动选择 ONVIF 模式
        document.querySelector('input[name="talkMode"][value="onvif"]').checked = true;
      } else {
        resultSpan.textContent = '✗ 不支持 ONVIF Backchannel';
        resultSpan.style.color = '#ff6b6b';
        audioInfo.textContent = '';
        this.log('该设备不支持 ONVIF Backchannel', 'warn');
        // 自动选择普通推流模式
        document.querySelector('input[name="talkMode"][value="normal"]').checked = true;
      }
    } catch (e) {
      resultSpan.textContent = '检测失败';
      resultSpan.style.color = '#ff6b6b';
      this.log(`ONVIF 检测错误: ${e.message || e}`, 'error');
    }
  }

  // 停止语音对讲
  stopTalk() {
    if (!this.talkPlayer) {
      this.log('对讲未启动', 'error');
      return;
    }
    this.talkPlayer.stopPush();
    this.talkPlayer.destroy();
    this.talkPlayer = null;
    this.log('语音对讲已停止', 'success');
  }

  // 更新播放时间显示
  updatePlayDurationDisplay(state) {
    if (this.playDurationTimer) {
      clearInterval(this.playDurationTimer);
      this.playDurationTimer = null;
    }
    if (state === 'playing' && this.playerType === 'media') {
      this.playDurationTimer = setInterval(() => {
        this.updateTimeDisplay();
        // 更新码率
        this.updateRateDisplay();
      }, 50);
    } else {
      document.getElementById('playDuration').textContent = '00:00 / 00:00';
      document.getElementById('progressFill').style.width = '0%';
      document.getElementById('videoRate').textContent = '0';
      document.getElementById('videoRateAvg').textContent = '0';
      document.getElementById('audioRate').textContent = '0';
      document.getElementById('audioRateAvg').textContent = '0';
      document.getElementById('lossRate').textContent = '0';
    }
  }

  // 更新时间显示
  updateTimeDisplay() {
    const player = this.players[0];
    if (!player) return;
    const position = player.getPosition();
    const duration = player.getDuration();
    const startTime = player.getStartTime();
    // console.log('pos:',position-startTime,);
    if (duration > 0) {
      // VOD: 显示 position / duration，用 startTime 修正偏移
      const relativePos = Math.max(0, position - startTime);
      const relativeDur = Math.max(0, duration - startTime);
      document.getElementById('playDuration').textContent =
        `${this.formatTime(relativePos)} / ${this.formatTime(relativeDur)}`;
      const progress = relativeDur > 0 ? Math.min(100, (relativePos / relativeDur) * 100) : 0;
      document.getElementById('progressFill').style.width = progress.toFixed(1) + '%';
    } else {
      // 直播: 用 position - startTime 显示已播放时间 + LIVE 标记
      const elapsed = Math.max(0, position - startTime);
      document.getElementById('playDuration').textContent =
        `${this.formatTime(elapsed)} LIVE`;
      document.getElementById('progressFill').style.width = '0%';
    }
  }

  // 点播相对 seek: delta 为毫秒, 正数前进, 负数后退
  seekBy(delta) {
    const player = this.players[0];
    if (!player) return;    
    const position = player.getPosition();
    player.seek(position + delta);    
    this.log(`Seek ${delta > 0 ? '+' : ''}${delta / 1000}s`, 'success');
  }

  // 更新码率和丢包率显示
  updateRateDisplay() {
    const player = this.players[0];
    if (!player) return;
    // TrackType: audio=1, video=2
    const videoRate = player.getRate(2, false) || 0;
    const videoRateAvg = player.getRate(2, true) || 0;
    const audioRate = player.getRate(1, false) || 0;
    const audioRateAvg = player.getRate(1, true) || 0;
    const lossRate = player.getLossRate ? (player.getLossRate(2) * 100) : 0;

    document.getElementById('videoRate').textContent = Math.round(videoRate);
    document.getElementById('videoRateAvg').textContent = Math.round(videoRateAvg);
    document.getElementById('audioRate').textContent = Math.round(audioRate);
    document.getElementById('audioRateAvg').textContent = Math.round(audioRateAvg);
    document.getElementById('lossRate').textContent = lossRate.toFixed(2);
  }

  // 格式化时间 (毫秒 -> HH:MM:SS 或 MM:SS)
  formatTime(ms) {
    const totalSeconds = Math.floor(Math.max(0, ms) / 1000);
    const hours = Math.floor(totalSeconds / 3600);
    const minutes = Math.floor((totalSeconds % 3600) / 60);
    const seconds = totalSeconds % 60;
    if (hours > 0) {
      return `${hours}:${String(minutes).padStart(2, '0')}:${String(seconds).padStart(2, '0')}`;
    }
    return `${String(minutes).padStart(2, '0')}:${String(seconds).padStart(2, '0')}`;
  }

  // 多屏缩放: 由autoSizeScale自动计算(基于canvas大小与视频分辨率的aspect-fit ratio)
  updateMultiScreenScale() {
    this.players.forEach(p => {
      if (p.setAutoSizeScale) {
        p.setAutoSizeScale(true);
      }
    });
    this.log(`多屏缩放: ${this.players.length}路, autoSizeScale已启用`, 'success');
  }

  // 绑定双击放大/恢复事件(多于1个播放器时才生效)
  bindDblClickFullscreen() {
    if (this.players.length <= 1) return;
    const cells = document.querySelectorAll('#videoGrid .grid-cell');
    cells.forEach((cell, index) => {
      cell.ondblclick = (e) => {
        e.preventDefault();
        e.stopPropagation();
        this.toggleFullscreenView(index);
      };
    });
  }

  // 双击切换: 放大某个播放器到全屏 / 恢复多屏
  toggleFullscreenView(index) {
    if (this.fullscreenIndex === -1) {
      // 当前多屏 -> 放大选中的到全屏
      this.fullscreenIndex = index;
      const cells = document.querySelectorAll('#videoGrid .grid-cell');
      cells.forEach((cell, i) => {
        if (i === index) {
          cell.style.display = 'flex';
          // 保存原始样式以便恢复
          cell.dataset.origGridCol = cell.style.gridColumn;
          cell.dataset.origGridRow = cell.style.gridRow;
          // 放大到整个grid
          cell.style.gridColumn = '1 / -1';
          cell.style.gridRow = '1 / -1';
        } else {
          cell.style.display = 'none';
        }
      });
      // autoSizeScale会根据canvas大小自动计算,全屏时canvas变大,scale自动关闭
      this.log(`双击放大: Player ${index} 全屏`, 'success');
    } else if (this.fullscreenIndex === index) {
      // 双击同一个播放器 -> 恢复多屏
      this.restoreMultiView();
    } else {
      // 双击另一个播放器 -> 切换到新的全屏
      this.restoreMultiView(true);
      this.toggleFullscreenView(index);
    }
  }

  // 恢复多屏视图
  restoreMultiView(silent = false) {
    if (this.fullscreenIndex === -1) return;
    const cells = document.querySelectorAll('#videoGrid .grid-cell');
    cells.forEach((cell) => {
      cell.style.display = 'flex';
      cell.style.gridColumn = cell.dataset.origGridCol || '';
      cell.style.gridRow = cell.dataset.origGridRow || '';
    });
    // 恢复多屏缩放状态
    const idx = this.fullscreenIndex;
    this.fullscreenIndex = -1;
    this.updateMultiScreenScale();
    if (!silent) {
      this.log(`双击恢复: Player ${idx} 还原多屏`, 'success');
    }
  }

  log(msg, level = 'info') {
    const container = document.getElementById('logContainer');
    const entry = document.createElement('div');
    entry.className = `log-entry log-${level}`;
    entry.textContent = `[${new Date().toLocaleTimeString()}] ${msg}`;
    container.appendChild(entry);
    container.scrollTop = container.scrollHeight;
  }

  switchPlayerType(type) {
    console.log(`切换到 ${type === 'media' ? 'MediaPlayer' : 'WebRtcPlayer'}`);
    this.playerType = type;
    this.createPlayers();
  }

  updateUIControls() {
    if (this.playerType === 'media') {
      document.getElementById('mediaControls').style.display = 'block';
      document.getElementById('webrtcControls').style.display = 'none';
    } else {
      document.getElementById('mediaControls').style.display = 'none';
      document.getElementById('webrtcControls').style.display = 'block';
    }
  }
}

// DOMContentLoaded 事件触发时初始化测试套件
document.addEventListener('DOMContentLoaded', () => {
  window.playerTestSuite = new PlayerTestSuite();
});
