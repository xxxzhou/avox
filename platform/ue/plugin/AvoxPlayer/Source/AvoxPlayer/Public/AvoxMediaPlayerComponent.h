#pragma once

#include <atomic>

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "AvoxEnums.h"
#include "AvoxMediaPlayerComponent.generated.h"

class FAvoxVideoBridge;
class FAvoxPlayerOb;
namespace avox { class IMediaPlayer; }

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FAvoxStateChanged, EAvoxPlayerState, State);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FAvoxReadySig);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FAvoxCompleteSig);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FAvoxErrorSig, int32, ErrorCode, const FString&, Message);

/**
 * avox 媒体播放器组件 (对应 godot 插件的 MediaPlayer)
 *
 * 播放 rtmp/rtsp/http/本地文件/torrent 等地址, 解码后的视频帧输出到 VideoTexture
 * (BGRA UTexture2D), 拖到材质参数或 UMG Image 上显示; 音频由 avox 内置渲染直接播放
 */
UCLASS(ClassGroup = (Avox), meta = (BlueprintSpawnableComponent))
class AVOXPLAYER_API UAvoxMediaPlayerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UAvoxMediaPlayerComponent();
	virtual ~UAvoxMediaPlayerComponent() override;

	// 播放地址 (rtmp/rtsp/http/本地路径/torrent 等)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Avox")
	FString Url;
	// 硬解开关 (下次 Open 生效)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Avox")
	bool bHardDecode = true;
	// 音量 0~1
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Avox", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Volume = 1.0f;
	// BeginPlay 时自动 Open
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Avox")
	bool bAutoPlay = false;
	// 视频帧纹理, 首帧后有效 (BGRA8)
	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Avox")
	UTexture2D* VideoTexture = nullptr;

	UPROPERTY(BlueprintAssignable, Category = "Avox")
	FAvoxStateChanged OnStateChanged;
	UPROPERTY(BlueprintAssignable, Category = "Avox")
	FAvoxReadySig OnReady;
	UPROPERTY(BlueprintAssignable, Category = "Avox")
	FAvoxCompleteSig OnComplete;
	UPROPERTY(BlueprintAssignable, Category = "Avox")
	FAvoxErrorSig OnError;

	// 打开并播放 (不传参用 Url 属性)
	UFUNCTION(BlueprintCallable, Category = "Avox")
	void Open(const FString& InUrl);
	UFUNCTION(BlueprintCallable, Category = "Avox")
	void Close();
	UFUNCTION(BlueprintCallable, Category = "Avox")
	void Pause();
	UFUNCTION(BlueprintCallable, Category = "Avox")
	void Resume();
	// seek 到指定毫秒
	UFUNCTION(BlueprintCallable, Category = "Avox")
	void Seek(int64 PositionMs);
	// 倍速播放
	UFUNCTION(BlueprintCallable, Category = "Avox")
	void SetSpeed(double InSpeed);
	UFUNCTION(BlueprintCallable, Category = "Avox")
	void SetHardDecode(bool bEnable);
	UFUNCTION(BlueprintCallable, Category = "Avox")
	void SetVolume(float InVolume);
	// IO 方案, 下次 Open 生效
	UFUNCTION(BlueprintCallable, Category = "Avox")
	void SetIoPlan(EAvoxIoPlan Plan);
	UFUNCTION(BlueprintPure, Category = "Avox")
	EAvoxPlayerState GetState() const;
	UFUNCTION(BlueprintPure, Category = "Avox")
	int64 GetDurationMs() const;
	UFUNCTION(BlueprintPure, Category = "Avox")
	int64 GetPositionMs() const;
	// 播放进度 0~1
	UFUNCTION(BlueprintPure, Category = "Avox")
	double GetProgress() const;
	UFUNCTION(BlueprintPure, Category = "Avox")
	bool IsPlaying() const;

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	friend class FAvoxPlayerOb;
	// avox 线程回调转发 (只入队不碰UE对象, Tick 时广播)
	void handleStateChange(EAvoxPlayerState state);
	void handleReady();
	void handleComplete();
	void handleError(int32 errorCode, const FString& message);

	void createPlayer();
	void destroyPlayer();
	void flushEvents();
	void updateTexture();

	// avox 播放器 (createMediaPlayer 裸 new, delete 是唯一释放路径)
	avox::IMediaPlayer* mediaPlayer = nullptr;
	FAvoxPlayerOb* playerOb = nullptr;
	FAvoxVideoBridge* videoBridge = nullptr;

	// 事件队列 (avox线程入队, 游戏线程 Tick 消费)
	struct FAvoxEvent
	{
		enum class EType { State, Ready, Complete, Error };
		EType type = EType::State;
		EAvoxPlayerState state = EAvoxPlayerState::None;
		int32 errorCode = 0;
		FString message;
	};
	FCriticalSection eventMutex;
	TArray<FAvoxEvent> pendingEvents;

	EAvoxIoPlan ioPlan = EAvoxIoPlan::None;
	double speed = 1.0;
	std::atomic<int> stateCache{ 0 };
	int32 textureWidth = 0;
	int32 textureHeight = 0;
};
