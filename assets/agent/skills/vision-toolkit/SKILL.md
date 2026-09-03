---
name: vision-toolkit
description: 让文本大模型"看到"图像。看图描述/提问、定位元素、抄录截图文字、比对两张图差异(改动前后)、裁剪、取主色、抠前景。用户给了图片/截图/视频帧, 或要我判断"图像在做什么"/比对"改动前后的图像区别"时用。
whenToUse: 需要理解图像内容(截图、视频帧、UI 快照)、在图中定位元素坐标、抄录截图上可见文字、比对参考图与改动后图的差异、裁剪图像区域、分析主色、抠前景时。需要视觉模型密钥 (agent.json 的 vision 节点)。
---

vision-toolkit 由底层视觉大模型 + 本地确定性图像处理组成。一句话分工:

- **要"理解"图 → 走视觉模型**: see-image / locate / ocr-screenshot。
- **要"算"图 → 本地即可, 不耗密钥**: crop / dominant-colors / extract-foreground / pixel-diff。
- **两图比对(改动前后) → pixel-diff 先定位, 再把差异框喂给 see-image 的 region 深看**。

依赖配置: 需要 agent.json 顶层 vision 节点置 enabled 并填 url/apiPath/apiKey/model。若调用直接报"视觉模型未配置完整", 请明确提示需要补配 vision 配置, 不要反复重试。

### 场景1: 让大模型知道图像在做什么
用 `see-image(image=<路径>)`, 默认会要求模型详细描述内容; 也可带 `query` 针对性提问。视频帧同理 —— 把帧抽成图片路径再传。
- 只关心文字 → mode=ocr 或直接用 ocr-screenshot, 按阅读顺序抄录, 更省 token。
- 需要元素坐标(按钮/输入框在哪) → 用 `locate(image, target)`, 返回像素框 x1,y1,x2,y2。

### 场景2: 比对改动前后的图像区别
典型流程:
1. `pixel-diff(original=改动前, rebuilt=改动后)` → 得到整体差异百分比 + 最差区块框 + 热力图(.diff.png) + 报告(.diff.json)。红=差异大, 青=差异小。
2. 对差异大的区块用 `see-image(image=改动后, region="x1,y1,x2,y2", query="这里和原图相比改了什么")` 深看, 结合热力图判断改动是否落到预期区域。
3. 需要更窄的窗口或放大细节 → `crop(image, region, scale)`。
4. 配色/主题是否一致 → `dominant-colors(image, region)`。

注意: rebuilt 与 original 尺寸不一致时会自动缩放并标注 scaled —— 尺寸差异本身也是发现, 不要忽略。

### 其他本地工具
- `crop(image, region[, scale, output])`: 裁指定像素框, 可放大 1-8 倍。裁前通常先 locate 拿框。
- `dominant-colors(image[, region, top])`: 区域主色及占比。
- `extract-foreground(image[, region, excludeColor])`: 前景(图标/Logo)抠成透明 PNG, 输出 <image>.fg.png。

### 顺序建议
看图先 describe → 需要坐标就 locate → 需要局部细节就 crop 后 see-image。比对改动先 pixel-diff 全图 → 再看热力图最差区块。所有产物路径都要在回复里带上, 让用户方便查看。