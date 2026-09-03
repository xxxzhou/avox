#pragma once

// 用于vulkan与dx11交互
// PS:
// https://github.com/krOoze/Hello_Triangle/blob/dxgi_interop/src/WSI/DxgiWsi.h
// https://github.com/roman380/VulkanSdkDemos/blob/d3d11-image-interop/BindImageMemory2/BindImageMemory2.cpp#L154

#include "../vulkan/VkTexture.hpp"
#include "avox_windows/dx11/Dx11Helper.hpp"
#include "avox_windows/dx11/Dx11Resource.hpp"
#include "avox_windows/dx12/Dx12Helper.hpp"

namespace avox {

enum InteropType { none, input, inputNt, output, outputNt };

class VkWinImage : public VkContextRef {
 private:
  /* data */
  // 外部输入非交互纹理,自动生成
  std::unique_ptr<Dx11SharedTex> shardTex = nullptr;
  // 外部输入的交互纹理 inputNt/outputNt
  Dx11SharedTex* exShardTex = nullptr;
  RenderType renderType = RenderType::D3D11;
  VkImage vkImage = VK_NULL_HANDLE;
  VkDeviceMemory memory = VK_NULL_HANDLE;
  ImageFormat format = {};
  bool bInit = false;
  // 对接是原始还是NT可交互GPU资源
  InteropType interopType = InteropType::none;
#if defined(VK_KHR_external_memory_win32)
  PFN_vkGetMemoryWin32HandleKHR vkGetMemoryWin32HandleKHR;
  PFN_vkGetMemoryWin32HandlePropertiesKHR vkGetMemoryWin32HandlePropertiesKHR;
#endif  // defined(VK_KHR_external_memory_win32)
#if defined(VK_KHR_external_fence_win32)
  // 外部 fence（从 DX11 导入）
  VkFence vkExternalFence = VK_NULL_HANDLE;
  PFN_vkImportFenceWin32HandleKHR vkImportFenceWin32HandleKHR = nullptr;
  HANDLE vkFenceHandle = nullptr;
#endif  // defined(VK_KHR_external_fence_win32)
 public:
  VkWinImage(/* args */);
  virtual ~VkWinImage();

 public:
  inline bool getInit() { return bInit; }
  inline VkDeviceMemory getDeviceMemory() { return memory; }
  inline const ImageFormat& getFormat() { return format; }
  inline VkImage getImage() { return vkImage; }
  inline HANDLE getHandle() { return shardTex ? shardTex->sharedHandle : nullptr; };
#if defined(VK_KHR_external_fence_win32)
  inline VkFence getExternalFence() { return vkExternalFence; }
#endif
  void release();
  void bindD3D(ID3D11Device* device, ImageFormat format);
  //
  RenderType getRenderType() { return renderType; }
  void setRenderType(RenderType type) { renderType = type; }
  // 把外部线程的GPU资源输入到当前VK管线中
  void updateInputContext(IRenderContext* context);
  // 把当前VK管线的结果输出到外部线程
  void updateOutputContext(IRenderContext* context);
  // Vk这边针对共享资源映射的DX纹理处理后,通知signal
  void signalFence();
#if defined(VK_KHR_external_fence_win32)
  // 初始化外部 fence（从 DX11 导入）
  bool initExternalFence(HANDLE dx11FenceHandle);
#endif 
};

}