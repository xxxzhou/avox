# vision-toolkit

让文本 Agent 具备"视觉能力"的 avox_agent 插件。它把本地图像处理与视觉模型调用桥接进 avox_agent 的工具体系，使文本模型可以描述、比对、定位并抄录图像（含视频帧）。

## 参考源项目

本插件移植自 **`dsh-vision-toolkit`**（仓库本地路径 `D:\Work\github\dsh-vision-toolkit`，为另一个仓库）。

dsh-vision-toolkit 采用三层结构：

- **Skill 指令资产**：指导模型何时、如何调用各视觉工具的 SKILL.md；
- **视觉工具集**：`see-image` / `pixel-diff` / `locate` / `ocr-screenshot` / `crop` / `dominant-colors` / `extract-foreground` 等；
- **底层视觉模型服务**：封装 OpenAI 兼容的多模态端点，把图片编码成 `data:image/<fmt>;base64,...` 数据 URL 送入 `image_url` 内容块。

本插件沿用同一结构，但底层全部改用 **C++**，不依赖 Python：

| dsh-vision-toolkit | 本插件实现 | 说明 |
| --- | --- | --- |
| Skill 资产（SKILL.md） | `assets/agent/skills/vision-toolkit/SKILL.md` | 流程编排资产，仅指令不写代码 |
| 视觉模型客户端 `vision_client.py` | `VisionHttp.cpp/hpp` | OpenAI 兼容 `chat/completions`，走 httplib |
| 视觉模型配置 | `adapter/ProviderCatalog.cpp/hpp` | 统一供应商目录：按能力从 model 节点收集 imageInput/imageOutput |
| 本地脚本 `pixel_diff.py` / crop | `VisionImage.cpp/hpp` | 复用 avox 的 stb_image（`AvoxImage.h`）做确定性图像处理 |
| 工具集中注册 | `VisionTools.cpp/hpp` | 7 个 `ToolDefinition` 工厂 |

### 工具映射

`VisionTools.hpp` 中保留了 dsh-vision-toolkit 工具集的同名语义（移除了依赖无头 Chrome / vtracer 外部二进制的 `html-screenshot` 与 `trace`）：

| 工具 | 来源 | 用途 |
| --- | --- | --- |
| `see-image` | `see-image` | 让文本模型理解单图 / 多图对比 |
| `pixel-diff` | `pixel-diff` | 两图逐像素差异 + 热力图 |
| `locate` | `locate` | 视觉模型返回元素像素框 |
| `ocr-screenshot` | `ocr-screenshot` | 抄录图像文字 |
| `crop` | `crop` | 本地裁剪 / 放大 |
| `dominant-colors` | `dominant-colors` | 主色 / 候选打分 |
| `extract-foreground` | `extract-foreground` | 抠前景透明 PNG |
| `generate-image` | （无，本插件新增） | 文生图（智谱 CogView 等） |

## 目录结构

```
vision-toolkit/
├── VisionHttp.cpp/hpp         # OpenAI 兼容 chat/completions 客户端 + 多图降级 + 失败切换
├── VisionImage.cpp/hpp        # 本地图像处理 (stb_image: 解码/裁剪/缩放/像素 diff)
├── VisionTools.cpp/hpp        # 8 个视觉工具的 ToolDefinition 工厂 + 统一注册
└── README.md
```

视觉供应商的解析与选择不在本插件目录，而在 `src/avox_agent/adapter/ProviderCatalog.cpp/hpp`（与
`FreeModelPool`、`DeploymentLoader` 同属 provider/adapter 域）。本插件的 `VisionTools` 在装配期
通过 `registerVisionTools(host, catalog, conversationImageInput)` 接收 `ProviderCatalog` 派生的供应商列表快照，工具调用
期只读快照，不碰目录本体。`VisionConfig`/`VisionProvider` 已并入 ProviderCatalog，不再单独建模。

## 配置

视觉能力统一由 `ProviderCatalog` 管理，供应商来自 `agent.json` 顶层的**模型节点**。每个模型节点用
一张**扁平能力表**声明自己的看家本领，给了同一个配置文件、运行时按能力自动选路：

| 字段 | 含义 |
| --- | --- |
| `free` | 是否免费 |
| `chat` | 支持文本对话 |
| `imageInput` | 支持图像理解（视觉输入） |
| `imageOutput` | 支持文生图（图像输出） |

图像理解走具备 `imageInput` 的节点，文生图走具备 `imageOutput` 的节点，文本走 `chat` 节点。
当前默认供应商不支持某能力时，自动切到下一个有能力且可用的节点（失败切换见下）。

### 顶层 vision 开关（可选）

```json
{
  "vision": {
    "enabled": true,
    "inputModel": "zhipu-vision",
    "outputModel": "zhipu-cogview"
  }
}
```

- `enabled`：视觉能力总开关。缺省时按「是否存在 `imageInput` 节点」自动推断；
- `inputModel` / `outputModel`：分别把图像理解、文生图的**偏好节点**置顶（同名节点优先）；
- 不写 `vision` 节点也可以，只要存在 `imageInput` 节点视觉就会被启用。

### 多 provider + 失败切换

`visionChat` / `visionImageGen` 按排序后的供应商列表逐个尝试，某端点返回 HTTP/网络错误时把它
「加热」冷却并自动切换下一个，直到成功——与文本免费模型（`FreeModelPool`）的失败切换体验一致。
冷却默认 15s，到期自动恢复。

### 零配置自动填充

若 `agent.json` **没有任何**模型节点，`ProviderCatalog::load` 自动填进一组内建**免费**节点：
智谱文本（`glm-4.7-flash`）、智谱视觉（`glm-4v-flash`）、智谱文生图（`cogview-3-flash`）与
anionex 视觉（公开密钥，开箱即用）。智谱三个节点的 `apiKey` 用占位符 `YOUR_ZHIPU_API_KEY`
标记——`ProviderEntry::ready()` 在密钥仍是占位符时返回 `false`，运行时**不会**把占位符当密钥发出；
装配时会用 `apiKeyHints()` 提示用户去智谱开放平台申请后替换。

启用视觉但没有任何 `imageInput` 节点时，自动兜底 anionex 免费端点，零密钥也能看图。

### 文生图（generate-image）

`generate-image` 走具备 `imageOutput` 的节点（智谱 CogView 等）调用 `images/generations`。
需要该节点 `apiKey` 已填（非占位符），否则工具返回可读错误，不影响其它视觉工具。

## 鉴权说明

所有视觉端点均使用 `Authorization: Bearer <apiKey>`（与 dsh-vision-toolkit 的 `vision_client.py` 完全一致）。anionex 免费端点的 `apiKey` 为公开常量；智谱的 `apiKey` 需自行申请。早期调试时曾因临时诊断脚本漏写 `Authorization:` 前缀而误判为需要把密钥放进请求体，已确认正确方式为 Bearer 头。