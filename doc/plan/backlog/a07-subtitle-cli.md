# A-7 avox_subtitle 管线 CLI

优先级 P1 · 里程碑 M3(M3 主菜,panvox P-6 AI 字幕嵌入化硬依赖) · 计划状态:就绪
VAD 切分 → sherpa 批量 STT → agent 翻译 → SRT 写出 → 缓存;断点续跑 + 批量队列。

## 出口判据

1. `avox_cli subtitle gen <media>` 一条命令产出 SRT(可选拼翻译),断点续跑有效。
2. 目录批量队列可跑;同 media 重跑命中缓存秒回。
3. 生成质量:时间轴对齐(startPts/endPts 误差可听辨),无乱码(GBK 输入亦然)。

## 现状(代码落点)

- **STT 能力在,形态是流式**:sherpa 插件 SenseVoice+Silero VAD
  (`plugins/avox_sherpa/SherpaSenseVoice.hpp:49,62-64`),接口 recognize(const AvoxData&, pts)
  (`src/avox/AvoxAudio.h:142`);`SttResult` 自带 startPts/endPts/lang/isFinal
  (`AvoxAudio.h:112-117`)——SRT 时间轴原料齐。**文件级批量入口不存在**,
  VAD 藏在 SenseVoice 内部未独立暴露。
- **翻译**:本地 ONNX 单语言对 ja→zh(`plugins/avox_translation/OnnxTranslator.hpp:26,64`);
  `ITranslator` 接口在 `src/avox/AvoxBase.h:43-60`;HttpTranslator 已折进 avox_zlmediakit
  (内置模块才链得到,复用受限);backlog 指定「agent 翻译」→ avox_agent provider 体系可用,
  CLI 范例 `src/avox_agent/AgentShell.hpp:15`。
- **CLI 惯例**:薄壳在 `src/avox_cmd/cli/`(avox_cli)+ 逻辑折进 avox.dll 经 C 导出
  (`src/avox_cmd/CmdExecute.h:16-38`);命令注册 `CmdRegistry.cpp:126-137`
  (现有 device/play/record/input/ops/python/vision/assets/voice,无字幕子命令)。
- **SRT 解析两份、写出为零**:文本路径 `src/avox/subtitle/SrtParser.cpp` +
  srt→ass 转换(`plugins/avox_ass/AssOverlay.cpp:92-132`);全仓无 SRT writer。
  **编码卫生没接字幕链**(a01-T2 同源问题,先修或共用)。
- 模型资产:`fetch_assets.py --select sherpa_sense_voice` 下载,CLI 可用性绑定资产在位。

## 任务拆解

- [ ] T1 批量转写管线(核心):媒体文件 → 解封装/音频解码 → 重采样 16k 单声道 PCM →
      VAD 分段 → 逐段 recognize → SttResult[]。解封装复用 avox_ffmpeg 能力,
      不起渲染、纯解码读文件。
- [ ] T2 SRT writer:新写(时间轴 HH:MM:SS,mmm 格式化 + Utf8 卫生接线),
      与 SrtParser 往返一致性用例。
- [ ] T3 翻译接入:agent(LLM)批量翻译——字幕行聚合翻(防逐句漂移),输出双语或替换行;
      OnnxTranslator ja→zh 作离线兜底;BYOK 走 panvox P-12 的 llm 配置。
- [ ] T4 CLI + 断点续跑:`subtitle gen` 子命令;中间产物(STT 段 json、翻译 json)按
      media hash 缓存,断点按阶段恢复;`--dir` 批量队列。
- [ ] T5 e2e:avox-test 加样片→SRT 用例(时间轴/内容抽验、RTF 实时率指标)。

## 验收

- 30 分钟样片生成 SRT,时间轴抽验偏差 <200ms;断点续跑:中断后重跑不重算已完成段;
  缓存命中重跑秒回。

## 风险与开放问题

- 流式接口喂批量文件的串接无人做过,重采样/PTS 对齐是新逻辑(SttResult 的 pts 口径要与
  媒体时间轴核对,AC3 等有 priming 的源注意偏移)。
- HttpTranslator 位置导致网络翻译要么走 agent、要么把通用 http 翻译抽出——先走 agent,别动 zlm。
- 资产在位性:CI/离线环境无 sherpa 模型时用例自动 skip 的口径要与 avox-test 约定。
