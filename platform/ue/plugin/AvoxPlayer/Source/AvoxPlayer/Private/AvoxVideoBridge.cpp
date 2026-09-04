#include "AvoxVideoBridge.h"
#include "AvoxPlayerModule.h"

FAvoxVideoBridge::~FAvoxVideoBridge()
{
	unbindSurface();
}

void FAvoxVideoBridge::bindSurface(avox::ISurfaceRender* surface)
{
	if (surfaceRender == surface) return;
	unbindSurface();
	if (!surface) return;
	surfaceRender = surface;
	// 离屏渲染 + YUV输出, setOffSurface传ytype即自动enableYuvOut
	surfaceRender->setVulkan(false);
	surfaceRender->setOffSurface(avox::YuvType::nv12);
	surfaceRender->enableYuvOut(avox::YuvType::nv12);
	avox::addSurfaceRenderOb(surfaceRender, this);
	UE_LOG(LogAvoxPlayer, Log, TEXT("video bridge bound to surface render"));
}

void FAvoxVideoBridge::unbindSurface()
{
	if (!surfaceRender) return;
	avox::removeSurfaceRenderOb(surfaceRender, this);
	surfaceRender = nullptr;
}

void FAvoxVideoBridge::onFrame(const avox::YUVFrame& frame)
{
	const int32_t w = frame.format.width;
	const int32_t h = frame.format.height;
	if (!frame.data[0] || w <= 0 || h <= 0) return;
	if (frame.format.type != avox::YuvType::nv12 && frame.format.type != avox::YuvType::yuv420P) return;
	TArray<uint8> bgra;
	bgra.AddUninitialized(w * h * 4);
	if (frame.format.type == avox::YuvType::nv12)
	{
		convertNv12(frame, bgra.GetData());
	}
	else
	{
		convertYuv420P(frame, bgra.GetData());
	}
	{
		FScopeLock lock(&mutex);
		frameData = MoveTemp(bgra);
		frameWidth = w;
		frameHeight = h;
	}
}

bool FAvoxVideoBridge::popFrame(TArray<uint8>& outBgra, int32& outWidth, int32& outHeight)
{
	FScopeLock lock(&mutex);
	if (frameData.Num() == 0) return false;
	outBgra = MoveTemp(frameData);
	frameData.Reset();
	outWidth = frameWidth;
	outHeight = frameHeight;
	return true;
}

void FAvoxVideoBridge::convertNv12(const avox::YUVFrame& frame, uint8* dst)
{
	const int32 w = frame.format.width;
	const int32 h = frame.format.height;
	const uint8* yPlane = frame.data[0];
	const uint8* uvPlane = frame.data[1];
	const int32 yStride = frame.stride[0];
	const int32 uvStride = frame.stride[1];
	// PF_B8G8R8A8: 字节序 B,G,R,A
	auto toByte = [](int v) { return (uint8)(v < 0 ? 0 : (v > 255 ? 255 : v)); };
	for (int32 j = 0; j < h; ++j)
	{
		const uint8* yRow = yPlane + j * yStride;
		const uint8* uvRow = uvPlane + (j / 2) * uvStride;
		uint8* dstRow = dst + (size_t)j * w * 4;
		for (int32 i = 0; i < w; ++i)
		{
			const int y = yRow[i];
			const int uvIdx = (i & ~1);
			const int u = uvRow[uvIdx] - 128;
			const int v = uvRow[uvIdx + 1] - 128;
			const int r = y + ((v * 1436) >> 10);
			const int g = y - ((u * 352 + v * 731) >> 10);
			const int b = y + ((u * 1815) >> 10);
			dstRow[i * 4] = toByte(b);
			dstRow[i * 4 + 1] = toByte(g);
			dstRow[i * 4 + 2] = toByte(r);
			dstRow[i * 4 + 3] = 255;
		}
	}
}

void FAvoxVideoBridge::convertYuv420P(const avox::YUVFrame& frame, uint8* dst)
{
	const int32 w = frame.format.width;
	const int32 h = frame.format.height;
	const uint8* yPlane = frame.data[0];
	const uint8* uPlane = frame.data[1];
	const uint8* vPlane = frame.data[2];
	const int32 yStride = frame.stride[0];
	const int32 uvStride = frame.stride[1] > 0 ? frame.stride[1] : w / 2;
	auto toByte = [](int v) { return (uint8)(v < 0 ? 0 : (v > 255 ? 255 : v)); };
	for (int32 j = 0; j < h; ++j)
	{
		const uint8* yRow = yPlane + j * yStride;
		const uint8* uRow = uPlane + (j / 2) * uvStride;
		const uint8* vRow = vPlane + (j / 2) * uvStride;
		uint8* dstRow = dst + (size_t)j * w * 4;
		for (int32 i = 0; i < w; ++i)
		{
			const int y = yRow[i];
			const int u = uRow[i / 2] - 128;
			const int v = vRow[i / 2] - 128;
			const int r = y + ((v * 1436) >> 10);
			const int g = y - ((u * 352 + v * 731) >> 10);
			const int b = y + ((u * 1815) >> 10);
			dstRow[i * 4] = toByte(b);
			dstRow[i * 4 + 1] = toByte(g);
			dstRow[i * 4 + 2] = toByte(r);
			dstRow[i * 4 + 3] = 255;
		}
	}
}
