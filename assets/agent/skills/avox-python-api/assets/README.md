# assets/ — 测试模板图标

本目录存放模板匹配测试素材, 供 `flows/loops.py` 的 `desktop_image_loop` / `window_image_loop` 函数使用。

| 文件 | 尺寸 | 说明 |
|------|------|------|
| `icon_red.png` | 64×64 | 红色圆角方块 (深灰底) |
| `icon_green.png` | 64×64 | 绿色圆形 (深灰底) |
| `icon_blue.png` | 64×64 | 蓝色三角形 (深灰底) |
| `test_scene.png` | 320×128 | 测试场景 (三图标横排, 灰底) |

## 用法

```python
from flows.loops import desktop_image_loop, desktop_text_loop
from flows.common import assets_path

# 桌面模板匹配 (先显示桌面, 找到后自动还原)
result = desktop_image_loop(assets_path('icon_red.png'), show_desktop=True)

# 桌面 OCR 文字匹配
result = desktop_text_loop("开始", show_desktop=True, double_click=True)

# 或传任意绝对路径
result = desktop_image_loop(r'D:/my_icons/chrome.png')
```

## 验证模板匹配

```python
from avox import Image, Vision
from flows.common import assets_path

scene = Image.loadImage(assets_path('test_scene.png'))
tmpl = Image.loadImage(assets_path('icon_red.png'))
matcher = Vision.createTemplateMatcher()
matcher.addTemplate(tmpl, 0.8)
print(matcher.match(scene))  # 应返回 1
```
