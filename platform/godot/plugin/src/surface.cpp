#include "surface.h"
#include "gpu_passthrough.h"

#include <godot_cpp/classes/rendering_device.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

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
    avox::addSurfaceRenderOb(surfaceRender, this);
}

void SurfaceTextureBridge::unbindSurface() {
    if (!surfaceRender) return;
    avox::removeSurfaceRenderOb(surfaceRender, this);

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

void SurfaceTextureBridge::onFrame(const avox::YUVFrame &frame) {
    if (gpuMode) return;
    int w = frame.format.width;
    int h = frame.format.height;
    if (!frame.data[0] || w <= 0 || h <= 0) return;
    int type = static_cast<int>(frame.format.type);

    // avox 可能交付 yuv420P(3平面) 或 nv12(交错UV), 必须按实际格式打包,
    // 否则把 yuv420P 当 nv12 读会把 U 平面误当 UV 交错、V 平面丢失 → 颜色错乱。
    PendingFrame pf;
    pf.width = w;
    pf.height = h;
    pf.type = type;
    int ySize = w * h;
    if (type == static_cast<int>(avox::YuvType::nv12)) {
        pf.data.resize(ySize + ySize / 2);
        uint8_t *dst = pf.data.ptrw();
        for (int i = 0; i < h; ++i) {
            memcpy(dst + i * w, frame.data[0] + i * frame.stride[0], w);
        }
        for (int i = 0; i < h / 2; ++i) {
            memcpy(dst + ySize + i * w, frame.data[1] + i * frame.stride[1], w);
        }
    } else if (type == static_cast<int>(avox::YuvType::yuv420P)) {
        int uvW = w / 2;
        int uvH = h / 2;
        int uSize = uvW * uvH;
        pf.data.resize(ySize + uSize * 2);
        uint8_t *dst = pf.data.ptrw();
        for (int i = 0; i < h; ++i) {
            memcpy(dst + i * w, frame.data[0] + i * frame.stride[0], w);
        }
        for (int i = 0; i < uvH; ++i) {
            memcpy(dst + ySize + i * uvW, frame.data[1] + i * frame.stride[1], uvW);
        }
        for (int i = 0; i < uvH; ++i) {
            memcpy(dst + ySize + uSize + i * uvW, frame.data[2] + i * frame.stride[2], uvW);
        }
    } else {
        return;  // 未知格式, 丢弃
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
    // 从 AVOX 拿 AHardwareBuffer (avox 核心导出到位前 getVkOutputHandle 返回 false → CPU 回退)
    avox::VkSharedHandle handle = {};
    if (!avox::getVkOutputHandle(surfaceRender, &handle)) return false;
    AHardwareBuffer *ahb = reinterpret_cast<AHardwareBuffer *>(handle.ahb);
    if (ahb == nullptr) return false;
    // 所有权转移到本类 (releaseSharedImage 时 release)
    AHardwareBuffer_acquire(ahb);
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
    importInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
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
    if (res != VK_SUCCESS) return false;

    // 查询内存需求
    VkMemoryRequirements memReqs = {};
    vkGetImageMemoryRequirements(device, importedImage, &memReqs);

#ifdef _WIN32
    // 导入外部内存 (NT 句柄)
    VkImportMemoryWin32HandleInfoKHR importMemInfo = {};
    importMemInfo.sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_WIN32_HANDLE_INFO_KHR;
    importMemInfo.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;
    importMemInfo.handle = reinterpret_cast<HANDLE>(handle.memHandle);
#else
    // 导入外部内存 (AHardwareBuffer)
    VkImportAndroidHardwareBufferInfoANDROID importMemInfo = {};
    importMemInfo.sType = VK_STRUCTURE_TYPE_IMPORT_ANDROID_HARDWARE_BUFFER_INFO_ANDROID;
    importMemInfo.buffer = ahb;
#endif

    VkMemoryAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReqs.size;
    allocInfo.pNext = &importMemInfo;

    // 内存类型选择: image bits ∩ handle bits
    uint32_t memoryTypeIndex = UINT32_MAX;
    uint32_t handleBits = 0xFFFFFFFFu;
#ifdef _WIN32
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
    {
        VkAndroidHardwareBufferPropertiesANDROID ahbProps = {};
        ahbProps.sType = VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_PROPERTIES_ANDROID;
        VkResult propsRes = vkGetAndroidHardwareBufferPropertiesANDROID(device, ahb, &ahbProps);
        handleBits = ahbProps.memoryTypeBits;
        UtilityFunctions::print("[avox_gpu] ahb memTypeBits=0x", (int64_t)handleBits,
                                " image memTypeBits=0x", (int64_t)memReqs.memoryTypeBits,
                                " propsRes=", (int)propsRes);
    }
#endif
    uint32_t inter = memReqs.memoryTypeBits & handleBits;
    if (inter == 0) {
        inter = memReqs.memoryTypeBits;
    }
    for (uint32_t i = 0; i < 32; ++i) {
        if (inter & (1u << i)) {
            memoryTypeIndex = i;
            break;
        }
    }
    if (memoryTypeIndex == UINT32_MAX) {
        UtilityFunctions::print("[avox_gpu] 无可用内存类型 (inter=0x", (int64_t)inter, ")");
        vkDestroyImage(device, importedImage, nullptr);
        importedImage = VK_NULL_HANDLE;
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
#endif
        return false;
    }

    res = vkBindImageMemory(device, importedImage, importedMemory, 0);
    UtilityFunctions::print("[avox_gpu] import vkBindImageMemory result=", (int)res);
    if (res != VK_SUCCESS) {
        vkFreeMemory(device, importedMemory, nullptr);
        importedMemory = VK_NULL_HANDLE;
        vkDestroyImage(device, importedImage, nullptr);
        importedImage = VK_NULL_HANDLE;
#ifndef _WIN32
        AHardwareBuffer_release(ahb);
#endif
        return false;
    }

    // 把 importedImage 收养为 Godot 纹理 (仅创建 image view, 底层 VkImage 仍归我们管)。
    // usage 用 SAMPLING_BIT — Godot 直接采样 importedImage, 不再做 per-frame copy。
    importedRid = rd->texture_create_from_extension(
        RenderingDevice::TEXTURE_TYPE_2D,
        RenderingDevice::DATA_FORMAT_R8G8B8A8_UNORM,
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
        return false;
    }

    // ═══════════════════════════════════════════════════════════
    // 2. 创建 Texture2DRD 直绑 importedRid — Godot 直接采样外部 memory image。
    //    注意: 这是跨 VkDevice 的外部内存 image, 直接采样在部分驱动 (Intel iGPU)
    //    可能崩溃 — 若崩, 回退到 copy-based 方案 (sampledImage + 每帧 texture_copy)。
    // ═══════════════════════════════════════════════════════════

    texRd.instantiate();
    texRd->set_texture_rd_rid(importedRid);

    UtilityFunctions::print("[avox_gpu] importSharedImage 完成: ", w, "x", h,
                            " (zero-copy: 直接采样 importedImage)");
    return true;
}

void SurfaceTextureBridge::releaseSharedImage() {
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
        // 等渲染线程泵入首帧后, VkVideoRender 才有 outputLayer。每帧重试 enableVkOutput
        // 直到成功 (PipeGraph::reset 只置标志, 实际重建在渲染线程, 主线程调用线程安全)。
        if (!gpuOutputEnabled && gpuW > 0 && surfaceRender) {
            if (avox::enableVkOutput(surfaceRender, gpuW, gpuH)) {
                gpuOutputEnabled = true;
                UtilityFunctions::print("[avox_gpu] enableVkOutput 成功 ", gpuW, "x", gpuH);
            }
        }
        if (gpuOutputEnabled && importedImage == VK_NULL_HANDLE && surfaceRender) {
            importSharedImage();
        }
        // 零拷贝直采: 无 per-frame copy。texture_copy 需要本机 image 才能随时 copy,
        // 跨 VkDevice 外部内存直采后 content 直接由 avox 渲染线程写入,
        // Godot 采样看到的永远是渲染线程最新写入的内容 (写读无显式同步, 靠驱动 memory barrier)。
        return;
    }

    // ── CPU 回退模式 ──
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

    PackedByteArray rgba;
    if (pf.type == static_cast<int>(avox::YuvType::yuv420P)) {
        rgba = yuv420pToRgba(pf.data.ptr(), pf.width, pf.height);
    } else {
        rgba = nv12ToRgba(pf.data.ptr(), pf.width, pf.height);
    }
    if (pf.width != lastWidth || pf.height != lastHeight || texture.is_null()) {
        Ref<Image> img = Image::create_from_data(pf.width, pf.height, false,
                                                  Image::FORMAT_RGBA8, rgba);
        texture = ImageTexture::create_from_image(img);
        lastWidth = pf.width;
        lastHeight = pf.height;
    } else {
        Ref<Image> img = Image::create_from_data(pf.width, pf.height, false,
                                                  Image::FORMAT_RGBA8, rgba);
        texture->update(img);
    }
}

Ref<Texture2D> SurfaceTextureBridge::getTexture() const {
    if (gpuMode) {
        return texRd;
    }
    return texture;
}

PackedByteArray SurfaceTextureBridge::nv12ToRgba(const uint8_t *nv12, int width, int height) {
    PackedByteArray rgba;
    rgba.resize(width * height * 4);
    uint8_t *out = rgba.ptrw();
    const uint8_t *yPlane = nv12;
    const uint8_t *uvPlane = nv12 + width * height;
    for (int j = 0; j < height; ++j) {
        for (int i = 0; i < width; ++i) {
            int yIdx = j * width + i;
            int uvIdx = (j / 2) * width + (i & ~1);
            int y = yPlane[yIdx];
            int u = uvPlane[uvIdx] - 128;
            int v = uvPlane[uvIdx + 1] - 128;
            int r = y + ((v * 1436) >> 10);
            int g = y - ((u * 352 + v * 731) >> 10);
            int b = y + ((u * 1815) >> 10);
            r = r < 0 ? 0 : (r > 255 ? 255 : r);
            g = g < 0 ? 0 : (g > 255 ? 255 : g);
            b = b < 0 ? 0 : (b > 255 ? 255 : b);
            int outIdx = yIdx * 4;
            out[outIdx] = r;
            out[outIdx + 1] = g;
            out[outIdx + 2] = b;
            out[outIdx + 3] = 255;
        }
    }
    return rgba;
}

PackedByteArray SurfaceTextureBridge::yuv420pToRgba(const uint8_t *yuv420p, int width, int height) {
    PackedByteArray rgba;
    rgba.resize(width * height * 4);
    uint8_t *out = rgba.ptrw();
    const uint8_t *yPlane = yuv420p;
    const uint8_t *uPlane = yuv420p + width * height;
    const uint8_t *vPlane = uPlane + (width / 2) * (height / 2);
    int uvW = width / 2;
    for (int j = 0; j < height; ++j) {
        for (int i = 0; i < width; ++i) {
            int yIdx = j * width + i;
            int uvIdx = (j / 2) * uvW + i / 2;
            int y = yPlane[yIdx];
            int u = uPlane[uvIdx] - 128;
            int v = vPlane[uvIdx] - 128;
            int r = y + ((v * 1436) >> 10);
            int g = y - ((u * 352 + v * 731) >> 10);
            int b = y + ((u * 1815) >> 10);
            r = r < 0 ? 0 : (r > 255 ? 255 : r);
            g = g < 0 ? 0 : (g > 255 ? 255 : g);
            b = b < 0 ? 0 : (b > 255 ? 255 : b);
            int outIdx = yIdx * 4;
            out[outIdx] = r;
            out[outIdx + 1] = g;
            out[outIdx + 2] = b;
            out[outIdx + 3] = 255;
        }
    }
    return rgba;
}

} // namespace godot
