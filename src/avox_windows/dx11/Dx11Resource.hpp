#pragma once
#include <memory>

#include "Dx11Context.hpp"
#include "Dx11Helper.hpp"

namespace avox {

class Dx11Resource {
 public:
  Dx11Resource() = default;
  virtual ~Dx11Resource() = default;

 protected:
  bool bBufferInit = false;

 public:
  uint8_t* cpuData = nullptr;

 public:
  // 释放资源并重置初始化状态
  void releaseResource();
  // 初始化资源（如果已初始化会先释放）
  bool initResource(ID3D11Device* deviceDx11);

 protected:
  virtual bool createResource(ID3D11Device* deviceDx11) = 0;
  virtual void releaseResourceImpl() = 0;

 public:
  virtual bool updateResource(ID3D11DeviceContext* ctxDx11) { return false; }
};

// 默认创建一个GPU可读写资源,UAV资源,非共享的
class Dx11CSResource : public Dx11Resource {
 public:
  Dx11CSResource() = default;
  virtual ~Dx11CSResource() = default;

 public:
  MComPtr<ID3D11ShaderResourceView> srvView = nullptr;
  MComPtr<ID3D11UnorderedAccessView> uavView = nullptr;

 protected:
  // cpu的数据可以提交到资源中,如果设定为true,自动不创建对应UAV资源
  bool bCpuWrite = false;
  // 是否是可以不同上下文都能访问的资源
  DX11SharedType sharedType = DX11SharedType::no;
  // 是否只创建一个纹理,默认创建相应的SRV/UAV资源
  bool bNoView = false;
  // 只创建UAV资源
  bool bOnlyUAV = false;
  // VideoProcessorOutputView/D3D11_BIND_RENDER_TARGET
  bool bVideoOut = false;
  // 用于截屏
  bool bGUI = false;

 public:
  // 默认为UAV资源,为ture对应SRV资源
  void setCpuWrite(bool bCpuWrite);
  // 默认不共享,为true是共享
  void setSharedType(DX11SharedType sharedType);
  DX11SharedType getSharedType() const { return sharedType; }
  // 默认创建相应UVA/SRV资源,为true不创建
  void setNoView(bool bNoView);
  void setOnlyUAV(bool bOnlyUAV);
  void setVideoOut(bool bVideo);
  void setGUI(bool gui);

 protected:
  void releaseResourceImpl() override;
};

class Dx11Texture : public Dx11CSResource {
 public:
  Dx11Texture() = default;
  virtual ~Dx11Texture() = default;

 public:
  MComPtr<ID3D11Texture2D> texture = nullptr;

 private:
  int32_t width = 0;
  int32_t height = 0;
  DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM;

 public:
  void setTextureSize(int32_t width, int32_t height,
                      DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM);
  int32_t getWidth() const { return width; }
  int32_t getHeight() const { return height; }
  DXGI_FORMAT getFormat() { return format; }

 protected:
  bool createResource(ID3D11Device* deviceDx11) override;
  void releaseResourceImpl() override;

 public:
  bool updateResource(ID3D11DeviceContext* ctxDx11) override;
};

class Dx11Buffer : public Dx11CSResource {
 public:
  Dx11Buffer() = default;
  virtual ~Dx11Buffer() = default;

 public:
  MComPtr<ID3D11Buffer> buffer = nullptr;

 private:
  int32_t elementSize = 0;
  int32_t dataType = 0;
  // Structured Buffer 需要最少四字节
  bool bRawBuffer = false;

 public:
  void setBufferSize(int32_t elementSize, int32_t dataType,
                     bool rawBuffer = false);

 protected:
  bool createResource(ID3D11Device* deviceDx11) override;
  void releaseResourceImpl() override;

 public:
  bool updateResource(ID3D11DeviceContext* ctxDx11) override;
};

class Dx11Constant : public Dx11Resource {
 public:
  Dx11Constant() = default;
  virtual ~Dx11Constant() = default;

 public:
  MComPtr<ID3D11Buffer> buffer = nullptr;

 private:
  int32_t byteDataSize = 0;

 public:
  void setBufferSize(int32_t dataType);

 protected:
  bool createResource(ID3D11Device* deviceDx11) override;
  void releaseResourceImpl() override;

 public:
  bool updateResource(ID3D11DeviceContext* ctxDx11) override;
};

class ShaderInclude : public ID3DInclude {
 public:
  ShaderInclude(std::string modelName, std::string rctype, int32_t rcId);
  ~ShaderInclude() {}

 public:
  // 通过 ID3DInclude 继承
  virtual HRESULT STDMETHODCALLTYPE Open(D3D_INCLUDE_TYPE IncludeType,
                                         LPCSTR pFileName, LPCVOID pParentData,
                                         LPCVOID* ppData,
                                         UINT* pBytes) override;
  virtual HRESULT STDMETHODCALLTYPE Close(LPCVOID pData) override;

 private:
  int32_t rcId = 0;
  std::string strRes = "";
  uint32_t length = 0;
};

}
