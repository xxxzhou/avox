# avox 语音输入 (tools/src/voiceinput)

Godot 全局语音输入工具:常驻悬浮窗,全局热键 → 麦克风 → sherpa 流式识别 → `SendInput` 逐字注入到当前焦点窗口(游戏聊天框/记事本)。行为对齐 `avox_cli voice`,设计稿见 [`docs/语音输入.md`](../../../docs/语音输入.md)。

## 运行

```powershell
# 前提: addons/avox_godot 已部署 (含 4 个语音类)
./platform/godot/plugin/deploy_godot.ps1 -GodotProject platform/godot/tools

# 首次打开项目需让编辑器扫描注册插件类
godot --path platform/godot/tools

# 直接跑语音输入窗口
godot --path platform/godot/tools res://src/voiceinput/voice_input.tscn
```

## 使用

- **无边框状态栏式悬浮条**(236×34, 默认屏幕右上角):顶栏 `[toggle|hold]` 是**单模式开关**(分段选择框, 激活段填蓝) —— 切到哪个模式就只听哪个热键;按住窗口任意空白可拖动(设置弹窗有「退出」真正退出)。
- **单模式 + 双热键**:toggle 记 `F9`(按一次开始录音,再按一次停止);hold 记 `F10`(按住说话,松开结束)。各模式各记各的热键,顶栏热键钮显示**当前模式**的热键,单击即可改绑。
- **hold 按住高亮**:hold 模式按住热键时热键钮呈按下状态。
- 识别文字实时注入到**当前焦点窗口**;悬浮窗下方**每次识别追加一行**。
- 关掉窗口只隐藏,热键仍生效(完全退出用设置弹窗「退出」)。
- 模型首次启动后台加载,未下载时设置弹窗提示 `python script/fetch_assets.py`。

## 文件

```
src/voiceinput/
├── voice_input.gd    # 主编排器 + 悬浮窗 (代码构建 UI, 同 mediaplayer 惯例)
├── voice_input.tscn  # 最小场景入口
└── key_capture.gd    # 热键捕获控件 (点一下按组合键)
```

## 已知限制

- **必须留在插件**:全局热键钩子与 `SendInput` 注入(Godot `_input` 只收本窗口事件)。
- 模型加载中按热键会提示"模型未就绪";模型就绪后才注册热键(对齐 cmdVoice 先模型后钩子)。
- v1 帧转发在主线程;识别密集时若卡,`MicCapture` 加 `forward_to(stt)` 直喂 tap 线程。
