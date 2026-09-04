#pragma once

#include "AvoxSdk.h"

// avox 视频帧 → UE 纹理桥接 (对应 godot 插件 SurfaceTextureBridge 的 CPU 路径)
//
// avox 渲染线程: ISurfaceRenderOb::onFrame 收 NV12/yuv420P, 转成 BGRA 存入单帧槽位
// UE 游戏线程:   popFrame 取最新帧, UpdateTextureRegions 异步上传到 UTexture2D
class FAvoxVideoBridge : public FNoncopyable, public avox::ISurfaceRenderOb
{
public:
	FAvoxVideoBridge() = default;
	virtual ~FAvoxVideoBridge();

	// 绑定 ISurfaceRender: 离屏渲染 + YUV输出(nv12), 内部注册观察者
	void bindSurface(avox::ISurfaceRender* surface);
	// 解绑, 需在播放器 close 之前调用 (surface render 要保持有效)
	void unbindSurface();
	avox::ISurfaceRender* getSurface() const { return surfaceRender; }

	// 游戏线程: 取最新 BGRA 帧, 无新帧返回 false
	bool popFrame(TArray<uint8>& outBgra, int32& outWidth, int32& outHeight);

private:
	// ── avox::ISurfaceRenderOb (avox 渲染线程调用) ──
	virtual void onFrame(const avox::YUVFrame& frame) override;
	virtual void onSurface() override {}
	virtual void onWinSizeChange(int32_t width, int32_t height) override {}

	// ── YUV → BGRA (BT.601 full-range, 同 godot 插件) ──
	static void convertNv12(const avox::YUVFrame& frame, uint8* dst);
	static void convertYuv420P(const avox::YUVFrame& frame, uint8* dst);

	avox::ISurfaceRender* surfaceRender = nullptr;
	// 单帧槽位 (mutex 保护, 新帧覆盖旧帧)
	FCriticalSection mutex;
	TArray<uint8> frameData;
	int32 frameWidth = 0;
	int32 frameHeight = 0;
};
