#pragma once

#include "Dx11Resource.hpp"

namespace avox {

// 跨线程共享纹理,二种主要方式
// 一是在不能控制输入/输出创建共享纹理时使用
//  做为输入时,别的线程结果->当前线程
//    别的线程interopTexture(false)到共享纹理中
//    当前线程从共享纹理中copyResource(true)到自身texture中
//  做为输出时,当前线程结果->别的线程,做为输出
//    当前线程从自身texture中copyResource(false)到共享纹理中
//    别的线程interopTexture(true)从共享纹理中获取结果
// 二是能控制输入/输出创建共享纹理时使用
//  在产生纹理结果的线程创建共享纹理作为输出
//  其NT句柄传递给使用别的线程共享使用
//  更为高效,但是需要在对共享纹理操作时,外部同步
class Dx11SharedTex : public IDx11Context {
 public:
  Dx11SharedTex();
  virtual ~Dx11SharedTex();

 public:
  DX11SharedType sharedType = DX11SharedType::sharedNT;

  // 自身Device
  ID3D11Device* device = nullptr;
  // 自身上下文
  MComPtr<ID3D11DeviceContext4> context4 = nullptr;
  // 自身Device中的纹理
  std::unique_ptr<Dx11Texture> texture = nullptr;
  // 自身Device由sharedFenceHandle打开的Fence
  MComPtr<ID3D11Fence> fence = nullptr;
  // 纹理共享句柄
  HANDLE sharedHandle = nullptr;

  // 记录交互的Device,从NT句柄里拿到资源比较费时
  ID3D11Device* interopDevice = nullptr;
  // 交互Device由sharedFenceHandle打开的Fence
  MComPtr<ID3D11Fence> interopFence = nullptr;
  // 交互Device由sharedHandle映射的缓存
  MComPtr<ID3D11Texture2D> interopTex = nullptr;
  // 交互的SRV
  MComPtr<ID3D11ShaderResourceView> interopSrv = nullptr;
  // NT同步共享句柄，由外部上下文打开得到ID3D11Fence
  HANDLE interopFenceHandle = nullptr;
  // 交互的上下文
  MComPtr<ID3D11DeviceContext> interopContext = nullptr;
  // 交互的sharedContext4
  MComPtr<ID3D11DeviceContext4> interopContext4 = nullptr;
  //
  ImageFormat vformat = {};

 protected:
  // 释放所有资源（可被子类重写）
  virtual void release();

 public:
  virtual ID3D11Device* getDevice() { return device; };
  virtual ID3D11Texture2D* getTexture() {
    if (texture && texture->texture) {
      return texture->texture.Get();
    }
    return nullptr;
  };
  virtual bool bInteropTexture() { return true; }

 public:
  void setDevice(ID3D11Device* device);
  // 获取自身的Dx11Texture,设置创建属性
  Dx11Texture* getDx11Texture();
  // 设置Dx11Texture的属性后,然后调用initTexture生成
  bool initTexture(ID3D11Device* deviceDx11);
  // 与自身上下文里的资源交互
  // shared2tex为true,表示从自身复制到texture
  void copyTexture(ID3D11Texture2D* destTex, bool shared2tex);
  // 与外部不同线程的DX环境交互（可被子类重写）
  virtual void interopTexture(IRenderContext* renderContext, bool shared2tex);

 public:
  // 设置交互的Device
  void setInteropDevice(ID3D11Device* device);
  // 必需在InteropDevice里调用
  ID3D11Texture2D* getInteropTexture();
  ID3D11ShaderResourceView* getInteropSrv();
  // 获取互操作 fence 的共享句柄（用于导入到 Vulkan）
  HANDLE getInteropFenceHandle() { return interopFenceHandle; }

  ImageFormat getImageFormat() const { return vformat; }
  // 当前Device能否读
  bool canRead();
  // 当前Device能否写
  bool canWrite();
  // 当上面操作完成后,需要通知fence
  void signalFence();
  // 交互Device能否读
  bool canInteropRead();
  // 交互Device能否写
  bool canInteropWrite();
  // 通知交互Device的fence
  void signalInteropFence();

 public:
  void logTex();
  // 请在InteropDevice里调用,查看交互的纹理状态
  void logInteropTex();

 private:
  // 交互Device变化,更新pBuffer/sharedFence等缓存资源
  void updateInteropDevice(ID3D11Device* device);
  // 释放共享句柄（保留纹理等资源）
  void releaseHandles();
};

bool copySharedToTexture(IDx11Context* context, Dx11SharedTex* sharedTex);

bool copyTextureToShared(IDx11Context* context, Dx11SharedTex* sharedTex);

}