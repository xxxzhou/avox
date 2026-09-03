# avox_sherpa

基于 [sherpa-onnx](https://github.com/k2-fsa/sherpa-onnx) 的语音识别模块。

## 功能

- 流式语音识别（低延迟）
- 离线语音识别（高精度）
- 多语言支持（中文、英文、日语、韩语、粤语）
- 自动语言检测
- 神经机器翻译（日语→中文）

## 组件

### SherpaRecognizer（流式模式）

- **用途**: 中英文实时语音识别
- **特点**: 低延迟，返回中间结果
- **模型**: `stt/zh-en` (中英文流式模型)
- **无翻译功能**

### SherpaSenseVoice（离线模式）

- **用途**: 多语言高精度语音识别
- **特点**: SenseVoice 模型，自动语言检测，带标点
- **模型**: `stt/sense-voice` (SenseVoice + Silero VAD)
- **支持翻译**: 非中文自动翻译成中文

## 使用

```cpp
#include "SherpaHelper.hpp"

// 流式模式（中英文）
AudioStt* stt = createAudioSttSherpa();

// 离线模式（多语言+翻译）
AudioStt* stt = createAudioSttSherpa();
stt->setTranslation(true);  // 开启翻译（日语→中文）

// 设置回调
stt->addObserver(new MySttObserver());

// 开始识别
stt->setAudioDesc(desc);
stt->loadModel();
stt->recognize(audioData);
```

## 模型文件

### 流式模型 (stt/zh-en)

```
assets/models/stt/zh-en/
├── encoder-epoch-99-avg-1.int8.onnx
├── decoder-epoch-99-avg-1.onnx
├── joiner-epoch-99-avg-1.int8.onnx
└── tokens.txt
```

### 离线模型 (stt/sense-voice)

```
assets/models/stt/sense-voice/
├── model.int8.onnx       # SenseVoice 模型
├── tokens.txt            # 词汇表
└── silero_vad.onnx       # VAD 模型
```

获取模型: `python assets/script/fetch_assets.py --select sherpa_sense_voice`（旧脚本 `script/sherpa/down_sense_voice.py`）

## 回调接口

```cpp
class IAudioSttOb {
  virtual void onResult(const char* text);       // 最终结果
  virtual void onPartialResult(const char* text); // 中间结果（仅流式）
  virtual void onEndpoint();                      // 检测到端点
};
```

## 依赖

- sherpa-onnx (ONNX Runtime 语音识别)
- avox_onnx (ONNX Runtime 推理)
- avox_translation (翻译模块，可选)
