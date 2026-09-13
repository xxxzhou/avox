# avox_ass — ASS/SSA(后续含 PGS)字幕 overlay 插件

内核封装 [libass](https://github.com/libass/libass), 把 ASS/SSA 特效字幕光栅化为
RGBA canvas(联合 bbox 裁剪), 由核心 VK PipeGraph 混合层(sourceOver)合成进输出帧。
设计文档: [doc/plan/player/ASS字幕渲染计划.md](../../doc/plan/player/ASS字幕渲染计划.md)。

## 依赖

libass + FriBidi + HarfBuzz(+FreeType)预编译在 **avc_library** 仓
`3rdparty/library/windows/ass/`(avc_library 惯例平台在前; 重编:
`python script/ass/build_windows.py`, 脚本在 avox 仓, 版本 pin 见脚本头注释):

- 产物在位(sibling 目录) → CMake 自动探测链接, 定义 `AVOX_ASS_HAVE_LIBASS=1`, 真实渲染;
- 产物缺失 → 只编骨架, `AssOverlay::init()` 返回 false, 运行期降级不崩。

## 链路

```
MKV ASS 轨(extradata+chunk) / 外挂 .ass/.srt
  → AssOverlay(libass) → AssCanvas{rgba,w,h,stride,x,y,pts,seq}
  → AvoxManager::assOverlayHub.create("libass")
  → 核心 VK 图层: canvas 变化时上传 → VkBlendLayer(sourceOverBlend)
  → 输出帧(字幕随帧到达消费端, 无需 App 叠层)
```

核心消费入口: `AvoxManager::Get().assOverlayHub` (接口 `src/avox/subtitle/IAssOverlay.hpp`)。
