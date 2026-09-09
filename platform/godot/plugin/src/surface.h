#pragma once

#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/texture2drd.hpp>
#include <godot_cpp/classes/rendering_device.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/sub_viewport.hpp>
#include <godot_cpp/classes/color_rect.hpp>
#include <godot_cpp/classes/shader.hpp>
#include <godot_cpp/classes/shader_material.hpp>

#include <avox/AvoxLayer.h>

#define VK_NO_PROTOTYPES
#include <volk.h>

#include <atomic>
#include <mutex>
#include <queue>

namespace godot {

// ColorSpaceDesc ↔ int: onReady(avox 线程)只存整数, 主线程再解回来喂纹理桥,
// 避免跨线程碰 Godot 对象。-1 表示未知(用桥的默认 BT.601 full)。
inline int encodeColorSpace(const avox::ColorSpaceDesc &cs) {
    return static_cast<int>(cs.standard) | (static_cast<int>(cs.range) << 8);
}
inline avox::ColorSpaceDesc decodeColorSpace(int code) {
    avox::ColorSpaceDesc cs;
    cs.standard = static_cast<avox::YuvStandard>(code & 0xFF);
    cs.range = static_cast<avox::YuvRange>((code >> 8) & 0xFF);
    return cs;
}

/// avox 视频帧 → Godot 纹理桥接
///
/// GPU 直通模式(Vulkan 后端, zero-copy):
///   avox::enableVkOutput → AVOX 创建带 external memory 的 VkImage (自己的 VkDevice)
///   avox::getVkOutputHandle → 拿 NT 共享句柄
///   插件用 Godot VkDevice 导入 NT 句柄 → importedImage (usage 含 SAMPLED, 跨 device OK)
///   texture_create_from_extension 收养 importedImage → Texture2DRD 直接采样
///   无每帧 texture_copy, 零 CPU 回读, 仅一次跨 VkDevice 导入。
///   注意: 跨 VkDevice 外部内存直采在部分驱动 (Intel iGPU) 会崩,
///   若崩则回退 copy-based (sampledImage + 每帧 texture_copy, 见 git 历史)。
///
/// CPU 回退模式(非 Vulkan 后端 / 初始化失败) — YUV 上传, shader 转 RGB:
///   avox 渲染线程: onFrame 把帧重打包成紧凑 NV12 入队 (yuv420P 顺手交织成 NV12,
///                  使下游只有一种布局)
///   Godot 主线程: update() 把 NV12 整块塞进一张 R8 纹理 (w × h*3/2, Y 在上 2/3,
///                  交错 UV 在下 1/3, 同 swig/nodejs/yuvglrender.js 的单图方案),
///                  内置 SubViewport + ColorRect + canvas_item shader 做 YUV→RGB,
///                  getTexture() 返回 SubViewport 的 ViewportTexture。
///   相比逐像素 CPU 转换: 上传量 w*h*1.5 而非 w*h*4, 无 per-pixel 乘加,
///   且矩阵/量程按源 ColorSpaceDesc 走 uniform (CPU 版是硬编码 BT.601 full)。
///   对外仍是一张普通 Texture2D, GDScript 侧无需挂 ShaderMaterial。
class SurfaceTextureBridge : public avox::ISurfaceRenderOb {
public:
    SurfaceTextureBridge();
    ~SurfaceTextureBridge();

    // ── 绑定 / 解绑 ISurfaceRender ──
    void bindSurface(avox::ISurfaceRender *surface);
    void unbindSurface();

    // GPU 直通开关 (默认开)。置 false 强制走 CPU 回退 (NV12 回读 + shader 转换)。
    // 必须在 bindSurface 之前设置才生效。
    void setGpuPassthroughEnabled(bool enable) { gpuPassthroughEnabled = enable; }
    bool getGpuPassthroughEnabled() const { return gpuPassthroughEnabled; }

    // 宿主 Node (MediaPlayer/SourcePlayer/RtcPlayer)。CPU 路径的 SubViewport 挂在其下,
    // 随宿主进出场景树。构造后立即设置; 未设置则 CPU 路径无法建立 (getTexture 返回空)。
    void setOwnerNode(Node *node) { ownerNode = node; }

    // ── ISurfaceRenderOb (avox 线程调用) ──
    void onFrame(const avox::YUVFrame &frame) override;
    void onSurface() override {}
    void onWinSizeChange(int32_t w, int32_t h) override;

    // ── Godot 主线程调用 ──
    void setVideoSize(int32_t w, int32_t h);  // onReady 后喂入真实尺寸, 触发 enableVkOutput
    // 源色彩空间 (onReady 后喂入)。同源驱动 avox 的 yuv2RGBA/rgba2YUV 矩阵与本类
    // shader 的解码矩阵; 幂等, 未变化直接返回。不喂则用默认 BT.601 full。
    void setColorSpace(const avox::ColorSpaceDesc &cs);
    void update();
    Ref<Texture2D> getTexture() const;

private:
    avox::ISurfaceRender *surfaceRender = nullptr;

    // ── GPU 直通模式 ──
    bool gpuPassthroughEnabled = true;  // 用户开关 (GDScript: MediaPlayer.gpu_passthrough)
    bool gpuMode = false;
    bool gpuOutputEnabled = false;  // enableVkOutput 已调用
    int importFailCount = 0;        // GPU 模式导入连续失败计数 (达阈值降级 CPU)
    std::atomic<bool> needReimport{false};  // avox 线程 onWinSizeChange 请求主线程重导 VkImage

    // 导入的外部 memory image (来自 avox 的 NT 句柄/AHB, usage=TRANSFER_SRC|DST|SAMPLED)
    VkImage importedImage = VK_NULL_HANDLE;
    VkDeviceMemory importedMemory = VK_NULL_HANDLE;
#ifdef __ANDROID__
    // avox 导出的 AHardwareBuffer(所有权转移到本类, releaseSharedImage 时 release)
    void *importedAhb = nullptr;
#endif

    // 收养 importedImage 的 RID (texture_create_from_extension, Godot 管理 image view)。
    // 直接采样该 RID, 无 per-frame copy, 零 CPU 回读。
    RID importedRid;          // 收养 importedImage (SAMPLING, 直采)
    Ref<Texture2DRD> texRd;
    int32_t gpuW = 0, gpuH = 0;

    bool importSharedImage();   // 导入 NT 句柄 + 收养 importedImage
    void releaseSharedImage();  // 释放所有 GPU 资源 (并清 gpuW/H, 等待 setVideoSize 重新喂入)
    void releaseImport();       // 仅释放导入资源, 保留 gpuW/H (图重建后同尺寸重导用)

    // ── CPU 回退模式 (YUV 上传 + shader 转换) ──
    // 布局统一为紧凑 NV12: [Y: h 行 × w] + [UV: h/2 行 × w (U/V 交错)],
    // 直接当成 w × h*3/2 的 R8 纹理上传, 由 shader 按行区寻址。
    struct PendingFrame {
        PackedByteArray data;
        int width = 0;
        int height = 0;
    };

    std::mutex mutex;
    std::queue<PendingFrame> queue;
    int lastWidth = 0;
    int lastHeight = 0;

    Node *ownerNode = nullptr;
    SubViewport *subViewport = nullptr;   // ownerNode 的子节点, 承载 YUV→RGB 这一趟
    ColorRect *yuvRect = nullptr;         // subViewport 内的全屏 rect, 挂 yuvMaterial
    Ref<Shader> yuvShader;
    Ref<ShaderMaterial> yuvMaterial;
    Ref<ImageTexture> yuvTexture;         // R8, w × h*3/2, 承载紧凑 NV12
    Ref<Texture2D> cpuTexture;            // subViewport 的 ViewportTexture (对外)

    avox::ColorSpaceDesc colorSpace = {};  // 默认 bt601/full, 与 avox 渲染管线默认一致
    bool colorSpaceSet = false;

    bool ensureCpuShaderPath(int w, int h);  // 建/重建 SubViewport + 纹理 (尺寸变化时重建)
    void destroyCpuShaderPath();
    void applyColorSpaceUniform();
};

} // namespace godot
