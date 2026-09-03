---
name: avox-python-api
description: avox SDK 的 Pythonic 高层 API 速查与写法。用 run_code/avox_cli python 写 avox 自动化、图像处理、视觉识别、播放/录制/采集/推流等功能时查这里。讲高层 avox 包(Image/Vision/Player/Source/Input/Muxer/Audio/Video)的类、枚举、最小示例, 优先用它而非原始 AvoxWrapper。
whenToUse: 用户要求用 python 写 avox 自动化、图像处理、视觉识别、播放/录制/采集/推流, 或用 run_code 写脚本、生成 python 工程时。
---

# avox Python 高层 API 速查

avox 的 C++ 能力已全部经 SWIG 暴露为原始绑定 `AvoxWrapper`, 高层包 `avox` 在其上做了 Pythonic 封装。
**写新代码优先用高层 `avox` 包**; 仅当高层未覆盖时才回退原始 `import AvoxWrapper as _pw`(进阶 YUV/矩阵等)。

高层包按模块惰性导入: `from avox import Image, Vision, Player, Source, Input, Muxer, Audio, Video`(大写是子模块)。
枚举集中在 `avox._core`(裸 IntEnum, 成员名自描述, 直接用 `Xxx.member`, 如 `MuxerType.ffmpeg`/`ACodecId.aac`/`VDeviceKind.monitor`; 也等价 `_pw.MuxerType_ffmpeg` 裸常量, 皆 int 可混用); 各模块 re-export 自己相关的枚举。全量成员见下方「枚举速查」(动态, 跑 `tools/dump_enums.py`)。

## 模块总览

| `from avox import` | 内容 | 关键类/函数 |
|---|---|---|
| `Image` | 图像缓冲与操作 | `IImageBuffer`, `loadImage`/`loadImageAsset`/`saveImage`/`resize`/`crop`/`toBase64`/`toBytes` |
| `Vision` | 视觉识别 | `ITemplateMatcher`, `ITextRecognizer`, `createTemplateMatcher`/`createTextRecognizer` |
| `Player` | 播放/采集/WebRTC | `IMediaPlayer`, `ISourcePlayer`, `IRtcPlayer` |
| `Source` | 设备源管理 (摄像头/桌面/窗口/麦/声卡回环) | `IVideoManager`, `IAudioManager`, `getVideoManager`/`getAudioManager` |
| `Input` | 输入注入/截图 | `IInputController`, `IScreenCapture`(视觉类已移到 Vision, 此处向后兼容 re-export) |
| `Muxer` | 封装/录制 | `IMediaMuxer`, `IRecorder` |
| `Audio` | 音频渲染/录音/语音识别 | `IAudioRender`, `IWavSave`, `IAudioStt`, `IAudioSttOb`, `createWavSave` |
| `Video` | 视频渲染 | `ISurfaceRender`, `IFontLayer`, `IGeometryLayer`, `IImageRender`, `createImageRender`, `canVulkan` |

## 写法参考 — 直接读源码 docstring

**写代码时按功能读对应源码**, 每个方法都有 docstring 说明参数类型和用途：

| 模块 | 运行时路径 (plugins/avox/) | 仓库源码 (开发时参考) |
|---|---|---|
| `avox.player` | `plugins/avox/player.py` | `swig/python/avox/player.py` |
| `avox.source` | `plugins/avox/source.py` | `swig/python/avox/source.py` |
| `avox.input` | `plugins/avox/input.py` | `swig/python/avox/input.py` |
| `avox.image` | `plugins/avox/image.py` | `swig/python/avox/image.py` |
| `avox.vision` | `plugins/avox/vision.py` | `swig/python/avox/vision.py` |
| `avox.muxer` | `plugins/avox/muxer.py` | `swig/python/avox/muxer.py` |
| `avox.audio` | `plugins/avox/audio.py` | `swig/python/avox/audio.py` |
| `avox.video` | `plugins/avox/video.py` | `swig/python/avox/video.py` |
| `avox._core` (枚举/桥) | `plugins/avox/_core.py` | `swig/python/avox/_core.py` |
| `avox._observer` | `plugins/avox/_observer.py` | `swig/python/avox/_observer.py` |
| 原始 SWIG 绑定 | `plugins/AvoxWrapper.py` | `swig/python/files/AvoxWrapper.py` |

运行时 `plugins/` 已在 `sys.path[0]` (SubprocessRunner/ensurePythonPath 注入), `import avox` 解析到 `plugins/avox/`。**优先用左列路径读源码**; 仓库源码 (右列) 仅开发机上有, 运行时不存在。

高层封装是原始绑定的薄包装, 每个类 `._native` 即对应 AvoxWrapper 原生对象。高层没封的方法可直接调 `obj._native.xxx()`。

**典型场景**: 想写"暂停/拖进度/变速"但不确定参数 → 读 `player.py` 源码, 看 `pause()/seek(pos)/speed(s)` 的 docstring。

## flows 索引 (端到端组合功能, 直接调 run)

| 功能 | 调用 | 说明 |
|---|---|---|
| 媒体→抽音频→STT字幕→中文字幕 | `flows/subtitle.py` → `run(media, outdir, stt='offline', translator='auto')` | 返回 `{ok,audio,srt,zh,segs,translated,errors}`; 缺模型/凭证→对应产出空+errors 记原因 |
| 抓桌面→模板匹配→点击重试 | `flows/loops.py` → `desktop_image_loop(tmpl, threshold=0.8, show_desktop=False, ...)` | 返回 `{ok,found,clicked,screenPos,matchType,matchInfo,retryCount,errors}` |
| 抓桌面→OCR文字匹配→点击重试 | `flows/loops.py` → `desktop_text_loop(text, threshold=0.3, show_desktop=False, ...)` | 子串匹配; 返回同上 |
| 抓窗口→模板匹配→点击重试 | `flows/loops.py` → `window_image_loop(tmpl, window, threshold=0.8, ...)` | window=窗口标题子串; 返回同上 |
| 抓窗口→OCR文字匹配→点击重试 | `flows/loops.py` → `window_text_loop(text, window, threshold=0.3, ...)` | 子串匹配; 返回同上 |
| 通用截图→识别→点击 | `flows/loops.py` → `vision_loop(target, mode='image', window=None, ...)` | mode='image'/'text'; window=None 抓桌面; 上四函数的通用版 |
| 采集桌面/窗口+麦/声卡→录文件/推流 | `flows/capture.py` → `run(output, video='desktop', audio='mic', bTranscode=True, watermark=None, adjust=None, ...)` | watermark/adjust 仅 bTranscode=True 进输出文件; video/audio 可 'none' 只录一轨; 返回 `{ok,output,videoSrc,audioSrc,duration,errors}`; `stop(dict)` 停持续采集 |
| 三宿主取帧(YUV/screenshot)+WAV | `flows/extract.py` → `run(src, out_dir, source='media', ...)` | `source='media'/'source'/'recorder'`(recorder=IRecorder 转码模式); `modes` 选 `('yuv','screenshot','wav')`; 三宿主均走 `setOffSurface` 离屏取帧; 返回 `{ok,yuv:{ok,frames},screenshot:{ok,frames},wav:{ok,path,duration},recorderOutput,errors}` |
| 结合AI大模型: 三宿主取帧→VLM画面描述 + 媒体→STT文字 | `flows/vlm.py` → `run(src, out_dir, source='media', modes=('vlm','stt'), lm_model='qwen3-vl-4b', ...)` | VLM 复用 extract 取帧喂 OpenAI 兼容大模型接口(默认本地 LM Studio, `lm_url`/`lm_model` 可改 vLLM/Ollama/远端); STT 仅 `source='media'` 文件复用 subtitle; `img_w`/`frame_count`/`max_tokens` 控速(集显带图慢, 详细描述~55s); 返回 `{ok,vlm:{ok,description,frames,elapsed},stt:{ok,text,srt,segs},errors}` |

**共享工具** (flows 内部互引): `flows/common.py` — `assets_path()`/`skill_dir()`(路径定位)、`pick_video_device()`/`pick_audio_device()`(设备选择)、`make_logger()`(日志)、`write_srt()`/`parse_srt()`(SRT 读写)。

**模板图标素材**: `assets/` 目录含测试用图标 (`icon_red/green/blue.png` + `test_scene.png`), 通过 `assets_path('icon_red.png')` 引用 (自动解析到 skill 目录)。也可传任意绝对路径给 `tmpl_path`。

## 心智模型与陷阱

**Player 是宿主管线**: 源进 → `getSurfaceRender()` 处理 → `getMuxer()`/Recorder 出。MediaPlayer/SourcePlayer/RtcPlayer 结构一致, 都带 `getSurfaceRender()`(取帧+处理) 与 `getMuxer(...)`(输出); 设备源经 `Source.getVideoManager/getAudioManager` 取(用 `.native`)。子对象借用原生, 生命周期归 Player/Manager——**不要自己销毁/open/close**, 走 `player.open()/close()`(`setAudioSource/setVideoSource` 可只挂一路; ISourcePlayer 无解码队列、无 A/V 同步)。

**关键陷阱** (非显然, 写前必知):
- **图像处理挂载点 = `getSurfaceRender()`**: player 的(也作用到该 player 的转码 muxer 输出)或 recorder 的(仅 `createRecorder(True)`); `IMediaMuxer` 本身没有。
- **IRecorder 不只是转码**: 原样保存/转发(remux)、转码、离线处理(out 空)三种用法见 muxer.py 源码 docstring; `bTranscode=True` 才有 `getSurfaceRender`, `getAudioRender()` 仅 TranscodeRecorder(True) 返裸 AudioRender、StreamRecorder(False) 返 None。
- **无窗口取帧用 `setOffSurface`**; `enableYuvOut` 只配合窗口 `setSurface`, 别当离屏替代。
- **关轨**: `setVideoCodec(VCodecId.none)`/`setAudioCodec(ACodecId.none)`(open 前), `IMediaMuxer` 与 `IRecorder` 都支持。
- **`getMuxer` 首次调用缓存**, 之后传不同 bTranscode 无效; `ISourcePlayer.getMuxer()` 无此参数(见 `flows/capture.py`)。
- **audio-only 录音输出必须 .mp4**: .aac/.m4a 在编码处崩溃。
- **异步操作用回调** (onReady/onComplete/onProgress 等), 不要轮询 state。

## Python 工程生成 (需求需持久化/带资产交付时,建工程而非散脚本)

- 目录: `%LOCALAPPDATA%/avox/projects/<name>/`(Windows) / `~/.local/share/avox/projects/<name>/`(Linux); `AVOX_PROJECTS_DIR` 可覆盖。与 SDK install 分离,升级不丢。
- 骨架(必含):
  ```
  <name>/main.py          # __main__ 自包含; 可选 def run(input,**kw)
  <name>/lib/             # >200 行拆这里
  <name>/assets/images/   # 工程私有图标/模板
  <name>/config.json      # 可调参数,别硬编码
  <name>/logs/            # run.log + shots/(可验证证据)
  <name>/SKILL.md         # 必需: name/description/script:main.py
  ```
- 资产定位: 工程私有图用 `loadImage` + `Path(__file__).parent`, 别用全局 `loadImageAsset`。
- 可验证性(脚本必须自证,人+AI 都能确认按需求走): 每步打日志(开始/结束/耗时/结果→logs/run.log 中文摘要); 关键节点截图留证(动作前后→logs/shots/); 动作后必须校验(OCR确认/匹配置信度),不达标报警不静默; 跑完给「成功/失败+证据清单」。
- 调试闭环: ① 逐步调试——run_code 跑短片段(秒级),stdout 直接回你,实时自纠; ② 全流程试跑——命令行 `python .../main.py` 直接跑(avox_cli 首跑已自注册 env, import avox 即通; 也可用 run_code script= 跑),事后用 read 读日志诊断; ③ 正式运行——同上,日志截图自存。
- 告诉用户怎么跑: 生成工程后让用户命令行 `python .../main.py` 直接跑(不必经 avox_cli; 前提是本机装了 avox 且 avox_cli/avox_agent 跑过一次自注册 env)。
- SOP: 起名(kebab) → 建骨架 → 写 main(含日志/截图/校验) → 截屏采集图标 → 自测(看 logs+shots) → 写工程 SKILL.md → 坑记进本 skill 的 LEARNINGS.md

## 枚举速查 (动态; 别背成员名)

**两处来源, 写法不同**:
- **包装枚举** `avox._core`: 写 `Xxx.member`(如 `MuxerType.ffmpeg`/`KeyCode.enter`)。**优先这个**。
- **裸常量** `AvoxWrapper`: 写 `_pw.Xxx_member`(_core 未封的进阶/内部枚举)。

**全量成员** → 跑 `python tools/dump_enums.py`(本 skill 目录; AST 解析不依赖 avox.dll; `--core-only`/`--raw-only` 分段)。源码改枚举脚本自动跟上, 成员名以输出为准。

**语义注记**(脚本只给成员名, 这些是写法/含义):
- `RecognizerType`(streaming=中英不支持日语/offline=离线)、`TranslatorType`(http=腾讯云、onnx=本地 ja→zh)、`AudioSttType`(目前 `sherpa`)、`Language`(zh/en/ja/other): 均为各 `create*`/`setTargetLanguage` 入参。
- `KeyCode`: 已 Pythonic 封装 → `KeyCode.enter`/`KeyCode.ctrl`/`KeyCode.f1`(不必 `_pw.KeyCode_*`)。
- `AudioAec` 是 **struct 不是枚举**: `_pw.AudioAec()` 构造, `.mobileMode` 为 int(0/1)。

## 自我维护（让本 skill 自动进化）

写脚本/用 run_code 时, 遇下列情况把经验追加到同目录 `LEARNINGS.md`:
- 踩坑并解决(报错/异常→绕过或修复), 且上文未写过;
- 用了上文没覆盖的非显然写法;
- 组合出新多步闭环模式(截图→识别→动作→反馈重试 等)。

**落盘前先确认**: 把要记的那一条(标题+现象+解法, 必要时含代码片段)先念给用户看, 用户认可后再追加进 `LEARNINGS.md`; 默认不自动写。

格式与蒸馏规则见 `LEARNINGS.md` 顶部(一事一条、最新在最上、软上限 150 行触发蒸馏回本文件对应小节)。**成功跑通的普通脚本不记**, 避免流水账。蒸馏时把稳定条目合并进本文件, 并从 LEARNINGS.md 删掉已吸收的。
