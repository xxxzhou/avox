const { app, BrowserWindow, session } = require('electron');
const path = require('path');

// GPU设置
// 1. 允许渲染进程访问 GPU 资源（解决 Display 为空的关键）
app.commandLine.appendSwitch('disable-gpu-sandbox');
// 2. 强制使用 D3D11 渲染后端（确保 EGL 走的是 ANGLE D3D 路径）
app.commandLine.appendSwitch('use-angle', 'd3d11');
app.commandLine.appendSwitch('enable-unsafe-webgpu');

let mainWindow;

// 配置 SharedArrayBuffer 安全头
app.on('ready', () => {
  console.log('[Main Process] Electron is ready');
  console.log('[Main Process] App GPU Info:', app.getGPUFeatureStatus());  
  mainWindow = new BrowserWindow({
    width: 1400,
    height: 1080,
    webPreferences: {
      sandbox: false,
      nodeIntegration: false,
      contextIsolation: true,  // 必须为 true 才能使用 contextBridge
      preload: path.join(__dirname, 'preload.js'), // 加载 preload 脚本
      webGL: true,  // 基础 WebGL 支持
      webGPU: true,  // 启用 WebGPU
      enableSharedArrayBuffer: true,  // 启用 SharedArrayBuffer
      experimentalFeatures: true,  // 启用实验性功能
      enableRemoteModule: false,
      // 简化的硬件加速设置
      hardwareAcceleration: true, 
    }
  });
  // 打开开发者工具
  // mainWindow.webContents.openDevTools();

  // 输出渲染进程的 GPU 信息
  mainWindow.webContents.on('did-finish-load', () => {
    // 等待 DOM 完全加载后再检查 WebGPU 可用性
    // 奇怪,主进程可能加载webgl,渲染进程加载不了     
    console.log('Window finished loading');
    const gpuInfo = app.getGPUFeatureStatus();
    console.log('GPU Feature Status:', JSON.stringify(gpuInfo, null, 2));
  });  
  // 加载测试页面
  mainWindow.loadFile('player_test.html');
  // mainWindow.loadFile('rtc_test.html');
});

app.on('window-all-closed', () => {
  if (process.platform !== 'darwin') {
    app.quit();
  }
});

