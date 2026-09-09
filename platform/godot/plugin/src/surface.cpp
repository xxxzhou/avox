#include "surface.h"
#include "gpu_passthrough.h"

#include <godot_cpp/classes/rendering_device.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/classes/viewport_texture.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/variant/vector2.hpp>
#include <godot_cpp/variant/vector2i.hpp>
#include <godot_cpp/variant/vector3.hpp>
#include <godot_cpp/variant/vector4.hpp>

#include <cstring>
#include <thread>

#ifdef _WIN32
#include <vulkan/vulkan_win32.h>
#elif defined(__ANDROID__)
#include <vulkan/vulkan_android.h>
#include <android/hardware_buffer.h>
#endif

namespace godot {

SurfaceTextureBridge::SurfaceTextureBridge() = default;

SurfaceTextureBridge::~SurfaceTextureBridge() {
    unbindSurface();
    // SubViewport 是 ownerNode 的子节点, 由宿主 Node 析构时统一回收。
    // 本类析构总是发生在宿主析构体内, 此时不能再调宿主的 remove_child,
    // 也不能 memdelete 一个仍在树内的节点 —— 只松开引用即可。
    cpuTexture.unref();
    subViewport = nullptr;
    yuvRect = nullptr;
    yuvTexture.unref();
    yuvMaterial.unref();
    yuvShader.unref();
}

void SurfaceTextureBridge::bindSurface(avox::ISurfaceRender *surface) {
    if (surfaceRender == surface) return;
    unbindSurface();
    if (!surface) return;
    surfaceRender = surface;

    if (gGpuPassthroughAvailable && gpuPassthroughEnabled) {
        gpuMode = true;
    } else {
        gpuMode = false;
        surfaceRender->setVulkan(false);
        surfaceRender->setOffSurface(avox::YuvType::nv12);
        surfaceRender->enableYuvOut(avox::YuvType::nv12);
    }
    // 重绑后把已知色彩空间重新下发 (play() 重建播放器时 surface 是新的)
    if (colorSpaceSet) surfaceRender->setColorSpace(colorSpace);
    avox::addSurfaceRenderOb(surfaceRender, this);
}

void SurfaceTextureBridge::unbindSurface() {
    if (!surfaceRender) return;
    avox::removeSurfaceRenderOb(surfaceRender, this);
    // 丢弃残帧, 避免下个 session 先显示上一路的画面
    {
        std::lock_guard<std::mutex> lock(mutex);
        std::queue<PendingFrame>().swap(queue);
    }

    if (gpuMode) {
        if (gpuOutputEnabled) {
            avox::disableVkOutput(surfaceRender);
            gpuOutputEnabled = false;
        }
        releaseSharedImage();
    }

    surfaceRender = nullptr;
}

// ── ISurfaceRenderOb (CPU 回退模式,avox 线程调用) ──

// frame.data 指向 VkVideoRender 的回读 buffer, 下一帧会被覆盖 —— 必须同步拷走。
// 统一打包成紧凑 NV12 (下游 shader 只认这一种布局): yuv420P 顺手交织成 NV12,
// 代价是 w*h/2 字节的字节级写, 远低于原先整帧 w*h*4 的 RGBA 转换。
void SurfaceTextureBridge::onFrame(const avox::YUVFrame &frame) {
    if (gpuMode) return;
    int w = frame.format.width;
    int h = frame.format.height;
    if (!frame.data[0] || w <= 0 || h <= 0) return;
    // NV12 打包要求偶数宽高 (avox 输出恒为偶数, 奇数直接丢弃避免越界)
    if ((w & 1) || (h & 1)) return;
    const avox::YuvType type = frame.format.type;
    if (type != avox::YuvType::nv12 && type != avox::YuvType::yuv420P) return;
    const int ySize = w * h;
    const int uvW = w / 2;
    const int uvH = h / 2;
    PendingFrame pf;
    pf.width = w;
    pf.height = h;
    pf.data.resize(ySize + ySize / 2);
    uint8_t *dst = pf.data.ptrw();
    // Y: 逐行去 stride padding
    for (int i = 0; i < h; ++i) {
        memcpy(dst + (size_t)i * w, frame.data[0] + (size_t)i * frame.stride[0], w);
    }
    uint8_t *uvDst = dst + ySize;
    if (type == avox::YuvType::nv12) {
        // UV 已交错, 一行正好 w 字节 (w/2 组 UV)
        for (int i = 0; i < uvH; ++i) {
            memcpy(uvDst + (size_t)i * w, frame.data[1] + (size_t)i * frame.stride[1], w);
        }
    } else {
        // yuv420P → NV12 交织。stride 缺省时退化为 w/2 (同原 CPU 路径的兜底)
        const int uStride = frame.stride[1] > 0 ? frame.stride[1] : uvW;
        const int vStride = frame.stride[2] > 0 ? frame.stride[2] : uvW;
        // avox 的 SwVideoBuffer::to() 在 rowPitch != width 时报的 stride 与实际布局
        // 不一致: 数据是 copyPlaneYUV2TightlyBuffer 产出的 GPU 打包布局
        // (每物理行 stride[0] 字节装 [逻辑行2p: uvW][逻辑行2p+1: uvW][pad]),
        // 而 stride[1] 报的是 unpack 后的等距值 rowPitch/2。按等距读会让奇数行
        // 左移 (rowPitch/2 - uvW) 个样本 → 竖条纹。这里按真实布局寻址。
        const bool gpuPacked = (uStride != uvW) && (frame.stride[0] > w);
        const int physPitch = frame.stride[0];
        for (int i = 0; i < uvH; ++i) {
            const uint8_t *uRow;
            const uint8_t *vRow;
            if (gpuPacked) {
                const size_t off = (size_t)(i / 2) * physPitch + (size_t)(i & 1) * uvW;
                uRow = frame.data[1] + off;
                vRow = frame.data[2] + off;
            } else {
                uRow = frame.data[1] + (size_t)i * uStride;
                vRow = frame.data[2] + (size_t)i * vStride;
            }
            uint8_t *o = uvDst + (size_t)i * w;
            for (int x = 0; x < uvW; ++x) {
                o[x * 2] = uRow[x];
                o[x * 2 + 1] = vRow[x];
            }
        }
    }
    {
        std::lock_guard<std::mutex> lock(mutex);
        while (queue.size() > 3) {
            queue.pop();
        }
        queue.push(std::move(pf));
    }
}

void SurfaceTextureBridge::onWinSizeChange(int32_t w, int32_t h) {
    (void)w;
    (void)h;
    if (gpuMode) {
        needReimport.store(true);
    }
}

// ── 主线程: onReady 后喂入真实视频尺寸, 启动 GPU 直通 ──

void SurfaceTextureBridge::setVideoSize(int32_t w, int32_t h) {
    if (!gpuMode || !surfaceRender || w <= 0 || h <= 0) return;
    if (!gpuOutputEnabled) {
        gpuW = w;
        gpuH = h;
    } else if (w != gpuW || h != gpuH) {
        releaseSharedImage();
        avox::disableVkOutput(surfaceRender);
        gpuOutputEnabled = false;
        gpuW = w;
        gpuH = h;
    }
}

// ── GPU 直通: 导入外部内存 image + 创建 Godot 采样目标 ──
// Windows: avox 导出 NT 句柄, VkImportMemoryWin32HandleInfoKHR 导入;
// Android: avox 导出 AHardwareBuffer(VkSharedHandle.ahb), VkImportAndroidHardwareBufferInfoANDROID 导入。

bool SurfaceTextureBridge::importSharedImage() {
    if (!surfaceRender) return false;

#ifdef _WIN32
    // 从 AVOX 拿 NT 共享句柄
    avox::VkSharedHandle handle = {};
    if (!avox::getVkOutputHandle(surfaceRender, &handle)) return false;
    if (handle.memHandle == 0) return false;
#else
    // 从 AVOX 拿 AHardwareBuffer。getVkOutputHandle 是转移语义 (返回的引用归本类),
    // releaseImport/失败路径 release 一次即可, 不得再 acquire (否则净泄漏一个引用)
    // 上次导入失败遗留的引用先释放 (导入仅在 importedImage==NULL 时发起, 无活跃绑定)
    if (importedAhb) {
        AHardwareBuffer_release(reinterpret_cast<AHardwareBuffer *>(importedAhb));
        importedAhb = nullptr;
    }
    avox::VkSharedHandle handle = {};
    if (!avox::getVkOutputHandle(surfaceRender, &handle)) return false;
    AHardwareBuffer *ahb = reinterpret_cast<AHardwareBuffer *>(handle.ahb);
    if (ahb == nullptr) return false;
    importedAhb = ahb;
#endif

    RenderingDevice *rd = RenderingServer::get_singleton()->get_rendering_device();
    if (!rd) return false;

    uint64_t vkDeviceU = rd->get_driver_resource(RenderingDevice::DRIVER_RESOURCE_LOGICAL_DEVICE, RID(), 0);
    VkDevice device = reinterpret_cast<VkDevice>(vkDeviceU);
    if (device == VK_NULL_HANDLE) return false;

    int32_t w = gpuW;
    int32_t h = gpuH;
    if (w <= 0 || h <= 0) return false;

#ifdef __ANDROID__
    // volk 设备扩展指针缺失 (未被加载) 时直接调用会 PC=0 崩溃, 先行防护并走降级
    if (!vkGetAndroidHardwareBufferPropertiesANDROID || !vkBindImageMemory2) {
        UtilityFunctions::print("[avox_gpu] volk fn missing ahbProps=",
                                (int64_t)(void*)vkGetAndroidHardwareBufferPropertiesANDROID,
                                " bind2=", (int64_t)(void*)vkBindImageMemory2);
        AHardwareBuffer_release(ahb);
        importedAhb = nullptr;
        return false;
    }
    // AHB 属性+格式一次查询: image format / allocationSize / 内存类型都以此为准
    // (对照仓内已验证可用的 VkAndImage::bindVK 写法)
    VkAndroidHardwareBufferFormatPropertiesANDROID ahbFmt = {};
    ahbFmt.sType = VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_FORMAT_PROPERTIES_ANDROID;
    VkAndroidHardwareBufferPropertiesANDROID ahbProps = {};
    ahbProps.sType = VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_PROPERTIES_ANDROID;
    ahbProps.pNext = &ahbFmt;
    VkResult ahbRes = vkGetAndroidHardwareBufferPropertiesANDROID(device, ahb, &ahbProps);
    UtilityFunctions::print("[avox_gpu] ahb props res=", (int)ahbRes, " format=", (int)ahbFmt.format,
                            " allocSize=", (int64_t)ahbProps.allocationSize,
                            " memTypeBits=0x", (int64_t)ahbProps.memoryTypeBits);
    if (ahbRes != VK_SUCCESS) {
        AHardwareBuffer_release(ahb);
        importedAhb = nullptr;
        return false;
    }
    // avox 导出端固定 R8G8B8A8, 查询值兜底防驱动回告不一致
    VkFormat ahbFormat = ahbFmt.format != VK_FORMAT_UNDEFINED
                             ? ahbFmt.format : VK_FORMAT_R8G8B8A8_UNORM;
#endif

    // ═══════════════════════════════════════════════════════════
    // 1. 导入外部 memory image (来自 avox 的另一个 VkDevice)
    //    usage 加 SAMPLED: usage 是 per-image 属性, 不受外部 memory block 约束,
    //    Godot 直接采样 importedImage, 不再每帧 texture_copy 到 sampledImage。
    //    仍需 TRANSFER_DST 与 avox 导出端一致 (导出端 usage=TRANSFER_SRC|DST)。
    // ═══════════════════════════════════════════════════════════

    VkExternalMemoryImageCreateInfo extMemImg = {};
    extMemImg.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
#ifdef _WIN32
    extMemImg.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;
#else
    extMemImg.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID;
#endif

    VkImageCreateInfo importInfo = {};
    importInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    importInfo.pNext = &extMemImg;
    importInfo.imageType = VK_IMAGE_TYPE_2D;
#ifdef __ANDROID__
    importInfo.format = ahbFormat;
#else
    importInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
#endif
    importInfo.extent = {static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1};
    importInfo.mipLevels = 1;
    importInfo.arrayLayers = 1;
    importInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    importInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    importInfo.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                       VK_IMAGE_USAGE_SAMPLED_BIT;
    importInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    importInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VkResult res = vkCreateImage(device, &importInfo, nullptr, &importedImage);
    UtilityFunctions::print("[avox_gpu] import vkCreateImage result=", (int)res);
    if (res != VK_SUCCESS) {
#ifndef _WIN32
        AHardwareBuffer_release(ahb);
        importedAhb = nullptr;
#endif
        return false;
    }

    // 查询内存需求
    VkMemoryRequirements memReqs = {};
    vkGetImageMemoryRequirements(device, importedImage, &memReqs);

    // 内存类型选择: image bits ∩ handle bits
    uint32_t memoryTypeIndex = UINT32_MAX;
    uint32_t handleBits = 0xFFFFFFFFu;
#ifdef _WIN32
    // 导入外部内存 (NT 句柄)
    VkImportMemoryWin32HandleInfoKHR importMemInfo = {};
    importMemInfo.sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_WIN32_HANDLE_INFO_KHR;
    importMemInfo.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;
    importMemInfo.handle = reinterpret_cast<HANDLE>(handle.memHandle);
    if (vkGetMemoryWin32HandlePropertiesKHR) {
        VkMemoryWin32HandlePropertiesKHR win32Props = {};
        win32Props.sType = VK_STRUCTURE_TYPE_MEMORY_WIN32_HANDLE_PROPERTIES_KHR;
        VkResult propsRes = vkGetMemoryWin32HandlePropertiesKHR(
            device, VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT,
            reinterpret_cast<HANDLE>(handle.memHandle), &win32Props);
        handleBits = win32Props.memoryTypeBits;
        UtilityFunctions::print("[avox_gpu] handle memTypeBits=0x", (int64_t)handleBits,
                                " image memTypeBits=0x", (int64_t)memReqs.memoryTypeBits,
                                " propsRes=", (int)propsRes);
    }
#else
    // 导入外部内存 (AHardwareBuffer)。
    // AHB 导入绑定 image 必须专用分配 (VkMemoryDedicatedAllocateInfo 与 import 同链,
    // 同 VkAndImage.cpp), 否则部分驱动 (Adreno) 在 bind 阶段直接 SIGSEGV 而非报错
    VkImportAndroidHardwareBufferInfoANDROID importMemInfo = {};
    importMemInfo.sType = VK_STRUCTURE_TYPE_IMPORT_ANDROID_HARDWARE_BUFFER_INFO_ANDROID;
    importMemInfo.buffer = ahb;
    VkMemoryDedicatedAllocateInfo dedicatedInfo = {};
    dedicatedInfo.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO;
    dedicatedInfo.pNext = &importMemInfo;
    dedicatedInfo.image = importedImage;
    handleBits = ahbProps.memoryTypeBits;
#endif

    VkMemoryAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
#ifdef _WIN32
    allocInfo.allocationSize = memReqs.size;
    allocInfo.pNext = &importMemInfo;
#else
    // AHB 导入的分配大小用 AHB 属性值, 非 image memReqs.size
    allocInfo.allocationSize = ahbProps.allocationSize;
    allocInfo.pNext = &dedicatedInfo;
#endif

    // 严格交集: AHB 导入不得回退 image 自身 bits (非法内存类型部分驱动直接崩)
    uint32_t inter = memReqs.memoryTypeBits & handleBits;
    if (inter == 0) {
        UtilityFunctions::print("[avox_gpu] no common memory type (image=0x", (int64_t)memReqs.memoryTypeBits,
                                " handle=0x", (int64_t)handleBits, ")");
        vkDestroyImage(device, importedImage, nullptr);
        importedImage = VK_NULL_HANDLE;
#ifndef _WIN32
        AHardwareBuffer_release(ahb);
        importedAhb = nullptr;
#endif
        return false;
    }
    for (uint32_t i = 0; i < 32; ++i) {
        if (inter & (1u << i)) {
            memoryTypeIndex = i;
            break;
        }
    }
    if (memoryTypeIndex == UINT32_MAX) {
        UtilityFunctions::print("[avox_gpu] no usable memory type (inter=0x", (int64_t)inter, ")");
        vkDestroyImage(device, importedImage, nullptr);
        importedImage = VK_NULL_HANDLE;
#ifndef _WIN32
        AHardwareBuffer_release(ahb);
        importedAhb = nullptr;
#endif
        return false;
    }
    UtilityFunctions::print("[avox_gpu] import memoryTypeIndex=", (int)memoryTypeIndex);
    allocInfo.memoryTypeIndex = memoryTypeIndex;

    res = vkAllocateMemory(device, &allocInfo, nullptr, &importedMemory);
    UtilityFunctions::print("[avox_gpu] import vkAllocateMemory result=", (int)res);
    if (res != VK_SUCCESS) {
        vkDestroyImage(device, importedImage, nullptr);
        importedImage = VK_NULL_HANDLE;
#ifndef _WIN32
        AHardwareBuffer_release(ahb);
        importedAhb = nullptr;
#endif
        return false;
    }

#ifdef _WIN32
    res = vkBindImageMemory(device, importedImage, importedMemory, 0);
#else
    // AHB 专用分配用规范绑法 bind2
    VkBindImageMemoryInfo bindInfo = {};
    bindInfo.sType = VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_INFO;
    bindInfo.image = importedImage;
    bindInfo.memory = importedMemory;
    bindInfo.memoryOffset = 0;
    res = vkBindImageMemory2(device, 1, &bindInfo);
#endif
    UtilityFunctions::print("[avox_gpu] import bindImageMemory result=", (int)res);
    if (res != VK_SUCCESS) {
        vkFreeMemory(device, importedMemory, nullptr);
        importedMemory = VK_NULL_HANDLE;
        vkDestroyImage(device, importedImage, nullptr);
        importedImage = VK_NULL_HANDLE;
#ifndef _WIN32
        AHardwareBuffer_release(ahb);
        importedAhb = nullptr;
#endif
        return false;
    }

    // 把 importedImage 收养为 Godot 纹理 (仅创建 image view, 底层 VkImage 仍归我们管)。
    // usage 用 SAMPLING_BIT — Godot 直接采样 importedImage, 不再做 per-frame copy。
    importedRid = rd->texture_create_from_extension(
        RenderingDevice::TEXTURE_TYPE_2D,
#ifdef __ANDROID__
        static_cast<RenderingDevice::DataFormat>(ahbFormat),
#else
        RenderingDevice::DATA_FORMAT_R8G8B8A8_UNORM,
#endif
        RenderingDevice::TEXTURE_SAMPLES_1,
        RenderingDevice::TEXTURE_USAGE_SAMPLING_BIT,
        reinterpret_cast<uint64_t>(importedImage),
        static_cast<uint64_t>(w),
        static_cast<uint64_t>(h),
        1, 1);
    UtilityFunctions::print("[avox_gpu] imported texture_create_from_extension rid_valid=", importedRid.is_valid());
    if (!importedRid.is_valid()) {
        vkFreeMemory(device, importedMemory, nullptr);
        importedMemory = VK_NULL_HANDLE;
        vkDestroyImage(device, importedImage, nullptr);
        importedImage = VK_NULL_HANDLE;
#ifndef _WIN32
        AHardwareBuffer_release(ahb);
        importedAhb = nullptr;
#endif
        return false;
    }

    // ═══════════════════════════════════════════════════════════
    // 2. 创建 Texture2DRD 直绑 importedRid — Godot 直接采样外部 memory image。
    //    注意: 这是跨 VkDevice 的外部内存 image, 直接采样在部分驱动 (Intel iGPU)
    //    可能崩溃 — 若崩, 回退到 copy-based 方案 (sampledImage + 每帧 texture_copy)。
    // ═══════════════════════════════════════════════════════════

    texRd.instantiate();
    texRd->set_texture_rd_rid(importedRid);

    UtilityFunctions::print("[avox_gpu] importSharedImage done: ", w, "x", h,
                            " (zero-copy: sampling importedImage directly)");
    return true;
}

void SurfaceTextureBridge::releaseImport() {
    RenderingDevice *rd = RenderingServer::get_singleton()->get_rendering_device();
    if (!rd) return;

    uint64_t vkDeviceU = rd->get_driver_resource(RenderingDevice::DRIVER_RESOURCE_LOGICAL_DEVICE, RID(), 0);
    VkDevice device = reinterpret_cast<VkDevice>(vkDeviceU);

    // 释放 Texture2DRD
    texRd.unref();

    // 释放 RD 纹理 (只销毁 image view, 底层 VkImage 由下面的 vkDestroyImage 管)
    if (importedRid.is_valid()) {
        rd->free_rid(importedRid);
        importedRid = RID();
    }

    // 释放 Vulkan 资源
    if (device != VK_NULL_HANDLE) {
        if (importedImage != VK_NULL_HANDLE) {
            vkDestroyImage(device, importedImage, nullptr);
            importedImage = VK_NULL_HANDLE;
        }
        if (importedMemory != VK_NULL_HANDLE) {
            vkFreeMemory(device, importedMemory, nullptr);
            importedMemory = VK_NULL_HANDLE;
        }
    }

#ifdef __ANDROID__
    // 释放 avox 转移来的 AHardwareBuffer 引用
    if (importedAhb != nullptr) {
        AHardwareBuffer_release(reinterpret_cast<AHardwareBuffer *>(importedAhb));
        importedAhb = nullptr;
    }
#endif
}

void SurfaceTextureBridge::releaseSharedImage() {
    releaseImport();
    // 维度一并复位: 尺寸变化后由 setVideoSize 重新喂入
    gpuW = 0;
    gpuH = 0;
}

// ── Godot 主线程每帧调用 ──

void SurfaceTextureBridge::update() {
    if (gpuMode) {
        // 主线程: 处理 avox 线程请求的尺寸变化 (释放旧 VkImage, 下次重新导入)
        if (needReimport.exchange(false)) {
            releaseSharedImage();
        }
        // 等渲染线程泵入首帧后, VkVideoRender 才有 outputLayer。
        // enableVkOutput 幂等(已激活直接 true), 每帧调用安全:
        // - 图重建(字幕/锐化等功能开关)后新 outputLayer 未激活 → 自动重新建立
        // - 图重建窗口期 getOutputLayer 返回空 → false, 下帧重试, 无竞态
        // (PipeGraph::reset 只置标志, 实际重建在渲染线程)
        if (gpuW > 0 && surfaceRender) {
            bool ok = avox::enableVkOutput(surfaceRender, gpuW, gpuH);
            if (ok && !gpuOutputEnabled) {
                // 首次建立或重建后重新建立: 新共享内存, 释放旧导入(保留维度), 下方重导
                releaseImport();
                gpuOutputEnabled = true;
                // 日志一律 ASCII: 插件编译无 /utf-8, 窄字面量中文在 Windows 必乱码
                UtilityFunctions::print("[avox_gpu] enableVkOutput ok ", gpuW, "x", gpuH);
            }
            gpuOutputEnabled = ok;
        }
        if (gpuOutputEnabled && importedImage == VK_NULL_HANDLE && surfaceRender) {
            if (importSharedImage()) {
                importFailCount = 0;
            } else if (++importFailCount >= 3) {
                // 连续导入失败 (驱动不支持/扩展缺失等): 一次性降级 CPU, 不再每帧重试。
                // 不调 setVulkan(false) — SurfaceRenderVk 运行中不支持切换,
                // 保留 Vulkan 管线, 改走 YUV 回调出 CPU 帧 (enableYuvOut → onFrame)。
                UtilityFunctions::print("[avox_gpu] import failed 3x, fallback to CPU path");
                avox::disableVkOutput(surfaceRender);
                gpuOutputEnabled = false;
                surfaceRender->setOffSurface(avox::YuvType::nv12);
                surfaceRender->enableYuvOut(avox::YuvType::nv12);
                gpuMode = false;
            }
        }
        // 零拷贝直采: 无 per-frame copy。texture_copy 需要本机 image 才能随时 copy,
        // 跨 VkDevice 外部内存直采后 content 直接由 avox 渲染线程写入,
        // Godot 采样看到的永远是渲染线程最新写入的内容 (写读无显式同步, 靠驱动 memory barrier)。
        return;
    }

    // ── CPU 回退模式: 上传 NV12 到 R8 纹理, SubViewport 内 shader 转 RGB ──
    PendingFrame pf;
    bool hasFrame = false;
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (!queue.empty()) {
            pf = std::move(queue.front());
            queue.pop();
            hasFrame = true;
        }
    }
    if (!hasFrame) return;
    if (!ensureCpuShaderPath(pf.width, pf.height)) return;
    // 整块 NV12 当成 w × h*3/2 的 R8 图: Y 在上 h 行, 交错 UV 在下 h/2 行
    Ref<Image> img = Image::create_from_data(pf.width, pf.height * 3 / 2, false,
                                            Image::FORMAT_R8, pf.data);
    if (img.is_null()) return;
    yuvTexture->update(img);
    // 只在有新帧时渲一次 (UPDATE_ONCE 渲完自动回到 DISABLED)
    subViewport->set_update_mode(SubViewport::UPDATE_ONCE);
}

Ref<Texture2D> SurfaceTextureBridge::getTexture() const {
    if (gpuMode) {
        return texRd;
    }
    return cpuTexture;
}

// ── CPU 回退模式: YUV→RGB 的 shader 转换趟 ──
// 一张 R8 纹理装整帧 NV12, ColorRect + canvas_item shader 在 SubViewport 里转成 RGB,
// 对外暴露 ViewportTexture, 所以 GDScript 侧仍是普通 Texture2D (无需 ShaderMaterial)。
// 用高层 Shader 而非 RD compute: CPU 回退的触发条件之一就是 RenderingDevice 为 null
// (Compatibility/OpenGL 后端), 那时 Texture2DRD 路线不存在。
static const char *kYuvToRgbShader = R"(shader_type canvas_item;
render_mode unshaded;

// 单张 R8: 上 yRows 行 Y, 下 yRows/2 行交错 UV (紧凑 NV12)
uniform sampler2D yuvTex : filter_nearest, repeat_disable;
uniform vec2 texSize;   // (w, h*1.5)
uniform float yRows;    // h
uniform vec4 coef;      // (rV, gU, gV, bU) 矩阵系数
uniform vec3 rangeAdj;  // (yBias, yScale, cScale) 量程展开

void fragment() {
    vec2 px = floor(UV * vec2(texSize.x, yRows));
    float y = (texture(yuvTex, (px + vec2(0.5)) / texSize).r - rangeAdj.x) * rangeAdj.y;
    // 色度行在 Y 之后, 每两条 Y 行共用一条; U/V 相邻两字节, 必须精确落在 texel 中心,
    // 否则线性过滤会把 U 和 V 混在一起
    float cy = yRows + floor(px.y * 0.5) + 0.5;
    float cx = floor(px.x * 0.5) * 2.0;
    float u = (texture(yuvTex, vec2(cx + 0.5, cy) / texSize).r - 0.5019608) * rangeAdj.z;
    float v = (texture(yuvTex, vec2(cx + 1.5, cy) / texSize).r - 0.5019608) * rangeAdj.z;
    COLOR = vec4(y + coef.x * v,
                 y - coef.y * u - coef.z * v,
                 y + coef.w * u,
                 1.0);
}
)";

bool SurfaceTextureBridge::ensureCpuShaderPath(int w, int h) {
    // SubViewport 要挂在宿主 Node 下才会渲染
    if (!ownerNode || !ownerNode->is_inside_tree()) return false;
    if (subViewport && w == lastWidth && h == lastHeight) return true;
    destroyCpuShaderPath();
    const int texH = h * 3 / 2;
    yuvShader.instantiate();
    yuvShader->set_code(String(kYuvToRgbShader));
    yuvMaterial.instantiate();
    yuvMaterial->set_shader(yuvShader);
    // 首帧前先填黑 (Y=16, U=V=128), 避免 update() 之前采到未初始化内存
    PackedByteArray init;
    init.resize((int64_t)w * texH);
    memset(init.ptrw(), 16, (size_t)w * h);
    memset(init.ptrw() + (size_t)w * h, 128, (size_t)w * h / 2);
    Ref<Image> img = Image::create_from_data(w, texH, false, Image::FORMAT_R8, init);
    if (img.is_null()) return false;
    yuvTexture = ImageTexture::create_from_image(img);
    if (yuvTexture.is_null()) return false;
    yuvMaterial->set_shader_parameter("yuvTex", yuvTexture);
    yuvMaterial->set_shader_parameter("texSize", Vector2((float)w, (float)texH));
    yuvMaterial->set_shader_parameter("yRows", (float)h);
    applyColorSpaceUniform();
    subViewport = memnew(SubViewport);
    subViewport->set_name("AvoxYuvConvert");
    subViewport->set_size(Vector2i(w, h));
    // 无新帧不重渲 (每帧 update() 有帧时才置 UPDATE_ONCE)
    subViewport->set_update_mode(SubViewport::UPDATE_DISABLED);
    subViewport->set_disable_3d(true);
    subViewport->set_transparent_background(false);
    subViewport->set_handle_input_locally(false);
    subViewport->set_default_canvas_item_texture_filter(
        Viewport::DEFAULT_CANVAS_ITEM_TEXTURE_FILTER_NEAREST);
    ownerNode->add_child(subViewport);
    yuvRect = memnew(ColorRect);
    yuvRect->set_position(Vector2(0, 0));
    yuvRect->set_size(Vector2((float)w, (float)h));
    yuvRect->set_material(yuvMaterial);
    subViewport->add_child(yuvRect);
    cpuTexture = subViewport->get_texture();
    lastWidth = w;
    lastHeight = h;
    // 日志一律 ASCII: 插件编译无 /utf-8, 窄字面量中文在 Windows 必乱码
    UtilityFunctions::print("[avox_cpu] yuv shader path ready ", w, "x", h,
                            " (R8 ", w, "x", texH, " -> viewport RGB)");
    return true;
}

// 仅用于分辨率变化时重建 (宿主仍存活)。宿主析构路径见 ~SurfaceTextureBridge。
void SurfaceTextureBridge::destroyCpuShaderPath() {
    cpuTexture.unref();
    // yuvRect 是 subViewport 的子节点, memdelete(subViewport) 会一并释放
    if (subViewport) {
        if (ownerNode && subViewport->get_parent() == ownerNode) {
            ownerNode->remove_child(subViewport);
        }
        memdelete(subViewport);
        subViewport = nullptr;
    }
    yuvRect = nullptr;
    yuvTexture.unref();
    yuvMaterial.unref();
    yuvShader.unref();
    lastWidth = 0;
    lastHeight = 0;
}

// 矩阵系数与 CPU 版 (Unity PlayerBridge::pickMatrix) 同参, Q10 定点转浮点
void SurfaceTextureBridge::applyColorSpaceUniform() {
    if (yuvMaterial.is_null()) return;
    Vector4 coef;
    switch (colorSpace.standard) {
        case avox::YuvStandard::bt709:
            coef = Vector4(1613.0f, 192.0f, 479.0f, 1900.0f);
            break;
        case avox::YuvStandard::bt2020:
            coef = Vector4(1510.0f, 168.0f, 585.0f, 1926.0f);
            break;
        default:
            coef = Vector4(1436.0f, 352.0f, 731.0f, 1815.0f);
            break;
    }
    coef /= 1024.0f;
    const bool limited = colorSpace.range == avox::YuvRange::limited;
    // limited(MPEG 16~235/240) → full: y=(y-16/255)*1.164062, c=(c-128/255)*1.138672
    Vector3 rangeAdj = limited ? Vector3(16.0f / 255.0f, 1.164062f, 1.138672f)
                               : Vector3(0.0f, 1.0f, 1.0f);
    yuvMaterial->set_shader_parameter("coef", coef);
    yuvMaterial->set_shader_parameter("rangeAdj", rangeAdj);
}

void SurfaceTextureBridge::setColorSpace(const avox::ColorSpaceDesc &cs) {
    if (colorSpaceSet && cs.standard == colorSpace.standard && cs.range == colorSpace.range) {
        return;
    }
    colorSpace = cs;
    colorSpaceSet = true;
    // 同源驱动 avox 的 yuv2RGBA/rgba2YUV 矩阵 (同 Unity PlayerBridge::onReady)。
    // 两个方向共用一个 colorSpace, 回读出的 YUV 与源同空间, 故 shader 用源矩阵解码。
    if (surfaceRender) surfaceRender->setColorSpace(cs);
    applyColorSpaceUniform();
}

} // namespace godot
