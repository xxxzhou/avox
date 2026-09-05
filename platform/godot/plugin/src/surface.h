#pragma once

#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/texture2drd.hpp>
#include <godot_cpp/classes/rendering_device.hpp>
#include <godot_cpp/classes/rendering_server.hpp>

#include <avox/AvoxLayer.h>

#define VK_NO_PROTOTYPES
#include <volk.h>

#include <atomic>
#include <mutex>
#include <queue>

namespace godot {

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
/// CPU 回退模式(非 Vulkan 后端 / 初始化失败):
///   avox 渲染线程: ISurfaceRenderOb::onFrame 推 YUV 帧进队列 (yuv420P 或 nv12)
///   Godot 主线程: update() 消费帧, YUV → RGBA, 更新 ImageTexture
class SurfaceTextureBridge : public avox::ISurfaceRenderOb {
public:
    SurfaceTextureBridge();
    ~SurfaceTextureBridge();

    // ── 绑定 / 解绑 ISurfaceRender ──
    void bindSurface(avox::ISurfaceRender *surface);
    void unbindSurface();

    // GPU 直通开关 (默认开)。置 false 强制走 CPU 回退 (NV12 回读 + nv12ToRgba)。
    // 必须在 bindSurface 之前设置才生效。
    void setGpuPassthroughEnabled(bool enable) { gpuPassthroughEnabled = enable; }
    bool getGpuPassthroughEnabled() const { return gpuPassthroughEnabled; }

    // ── ISurfaceRenderOb (avox 线程调用) ──
    void onFrame(const avox::YUVFrame &frame) override;
    void onSurface() override {}
    void onWinSizeChange(int32_t w, int32_t h) override;

    // ── Godot 主线程调用 ──
    void setVideoSize(int32_t w, int32_t h);  // onReady 后喂入真实尺寸, 触发 enableVkOutput
    void update();
    Ref<Texture2D> getTexture() const;

private:
    avox::ISurfaceRender *surfaceRender = nullptr;

    // ── GPU 直通模式 ──
    bool gpuPassthroughEnabled = true;  // 用户开关 (GDScript: MediaPlayer.gpu_passthrough)
    bool gpuMode = false;
    bool gpuOutputEnabled = false;  // enableVkOutput 已调用
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

    // ── CPU 回退模式 ──
    struct PendingFrame {
        PackedByteArray data;   // 紧凑打包: Y + UV(nv12) 或 Y + U + V(yuv420P)
        int width = 0;
        int height = 0;
        int type = 0;           // avox::YuvType, 决定 data 布局
    };

    std::mutex mutex;
    std::queue<PendingFrame> queue;
    Ref<ImageTexture> texture;
    int lastWidth = 0;
    int lastHeight = 0;

    static PackedByteArray nv12ToRgba(const uint8_t *nv12, int width, int height);
    static PackedByteArray yuv420pToRgba(const uint8_t *yuv420p, int width, int height);
};

} // namespace godot
