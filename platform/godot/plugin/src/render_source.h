#pragma once

// IRenderSource: 统一"渲染数据源"mixin。
// 任何持有 avox::ISurfaceRender 的 Godot 节点(MediaPlayer/SourcePlayer/MediaRecorder)
// 实现此接口, 供 VideoFaceNode 等消费者经 dynamic_cast<IRenderSource*> 取 surface
// 做零拷贝回读 tap (enableImage), 与具体数据源类型解耦 —— 切换数据源只需换节点。
// 纯抽象: 无数据成员, 不继承 Object, 多继承安全。

namespace avox {
class ISurfaceRender;
}

namespace godot {

class IRenderSource {
 public:
  virtual ~IRenderSource() = default;
  // 返回底层 surface render; 可能为 null(节点未 open, 或直录模式不解码无 surface)。
  // 调用方不持有, 生命周期归节点; player 类每轮 play 会重建 surface, 故需每次现取。
  virtual avox::ISurfaceRender* getSurfaceRenderRaw() = 0;
};

}  // namespace godot
