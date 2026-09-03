#pragma once

#include <d3d11.h>
#include <d3d11_4.h>
#include <d3dcommon.h>
#include <d3dcompiler.h>

#include "../WinCommon.hpp"
#include "avox/AvoxVideo.h"

#define AVOX_DX11_MUTEX_READ 1
#define AVOX_DX11_MUTEX_WRITE 0
#define AVOX_ENABLE_DX11VA 0

namespace avox {

enum class DX11SharedType {
  no,
  shared,
  sharedNT,
};

DXGI_FORMAT getImageDXFormt(ImageType imageType);
ImageType getImageType(DXGI_FORMAT imageType);
YuvType getDxFormat(DXGI_FORMAT format);
// 创建一个Dx11环境
// bMultithread=true 时去掉 SINGLETHREADED 标志(设备默认多线程保护),
// 供需要跨线程访问的场景(如 WinRT 抓帧帧池)
bool createDevice11(ID3D11Device **deviceDx11, ID3D11DeviceContext **ctxDx11,
                    bool bMultithread = false);
// 从CPU数据传入GPU中
bool updateDx11Resource(ID3D11DeviceContext *ctxDx11, ID3D11Resource *resouce,
                        uint8_t *data, uint32_t size);
// byteWidth不一定和width*elementSize相等,需要注意处理
bool downloadDx11Resource(ID3D11DeviceContext *ctxDx11, ID3D11Resource *resouce,
                          uint8_t **data, uint32_t &byteWidth);
// DX11截屏
bool createGUITextureBuffer(ID3D11Device *deviceDx11, int width, int height,
                            ID3D11Texture2D **ppBufOut);
// 用于创建一块CPU不能读的BUFFER到相同规格CPU能读的BUFFER
void copyBufferToRead(ID3D11Device *deviceDx11, ID3D11Buffer *pBuffer,
                      ID3D11Buffer **descBuffer);
void copyBufferToRead(ID3D11Device *deviceDx11, ID3D11Texture2D *pBuffer,
                      ID3D11Texture2D **descBuffer);
// SRV:描述了诸如贴图时,一般用来做CS中的输入,但是ID3D11Texture2D相应的SRV满足特定几种格式也可以做输入,ID3D11Buffer的SRV没限制
// UAV:描述了可以由Shader进行随机读写的显存存空间,用于做CS中的输入与输出
bool createBufferSRV(ID3D11Device *deviceDx11, ID3D11Buffer *pBuffer,
                     ID3D11ShaderResourceView **ppSRVOut);
bool createBufferUAV(ID3D11Device *deviceDx11, ID3D11Buffer *pBuffer,
                     ID3D11UnorderedAccessView **ppUAVOut);
bool createBufferSRV(ID3D11Device *deviceDx11, ID3D11Texture2D *pBuffer,
                     ID3D11ShaderResourceView **ppSRVOut);

bool createBufferUAV(ID3D11Device *deviceDx11, ID3D11Texture2D *pBuffer,
                     ID3D11UnorderedAccessView **ppUAVOut);
// 得到D3D11资源的非NT共享句柄
HANDLE getDx11SharedHandle(ID3D11Resource *source);

// NT+KeyedMutex,暂时没用到这种组合
void copySharedToTexture(ID3D11Device *d3ddevice, const HANDLE &sharedHandle,
                         ID3D11Texture2D *texture, bool bNT = false);

// NT+KeyedMutex,暂时没用到这种组合
void copyTextureToShared(ID3D11Device *d3ddevice, const HANDLE &sharedHandle,
                         ID3D11Texture2D *texture, bool bNT = false);

int32_t sizeDxFormatElement(DXGI_FORMAT format);

bool createAndCopyToDebugBuf(ID3D11Device *deviceDx11, ID3D11Texture2D *texture,
                             ID3D11Texture2D **debugTex);
bool getTextureData(ID3D11Device *deviceDx11, ID3D11Texture2D *texture,
                    IImageBuffer *out);

bool getSuitAdapter(IDXGIAdapter1 **adapter);

bool getImageFormat(ID3D11Texture2D *texture, ImageFormat &imageFormat);

void logTexture(ID3D11Device *d3ddevice, ID3D11Texture2D *texture);

}