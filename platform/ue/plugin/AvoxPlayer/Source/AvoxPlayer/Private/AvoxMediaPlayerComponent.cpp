#include "AvoxMediaPlayerComponent.h"
#include "AvoxPlayerModule.h"
#include "AvoxSdk.h"
#include "AvoxVideoBridge.h"
#include "Engine/Texture2D.h"

// avox 播放器观察者: avox线程回调 → 组件入队, 持裸指针
// 生命周期由组件保证 (destroyPlayer 先删 ob, 再动组件)
// 全局作用域: 头文件里的 friend class FAvoxPlayerOb 指向 ::FAvoxPlayerOb
class FAvoxPlayerOb : public avox::IMediaPlayerOb
{
public:
	explicit FAvoxPlayerOb(UAvoxMediaPlayerComponent* inOwner) : owner(inOwner) {}

	virtual void onStateChange(avox::PlayerState preState, avox::PlayerState state) override
	{
		(void)preState;
		owner->handleStateChange(toUeState(state));
	}
	virtual void onReady() override { owner->handleReady(); }
	virtual void onComplete() override { owner->handleComplete(); }
	virtual void onIoError(avox::AVError error, const char* msg) override
	{
		owner->handleError((int32)error, FString::Printf(TEXT("io error: %s"), msg ? UTF8_TO_TCHAR(msg) : TEXT("")));
	}
	virtual void onDecodeError(avox::TrackType trackType, avox::DecodeResult error) override
	{
		owner->handleError((int32)error, FString::Printf(TEXT("decode error [%s]: %s"),
			UTF8_TO_TCHAR(avox::getTrackTypeStr(trackType)), UTF8_TO_TCHAR(avox::getDecodeResultStr(error))));
	}

private:
	static EAvoxPlayerState toUeState(avox::PlayerState state)
	{
		switch (state)
		{
		case avox::PlayerState::opening: return EAvoxPlayerState::Opening;
		case avox::PlayerState::ready: return EAvoxPlayerState::Ready;
		case avox::PlayerState::playing: return EAvoxPlayerState::Playing;
		case avox::PlayerState::pause: return EAvoxPlayerState::Pause;
		case avox::PlayerState::seek: return EAvoxPlayerState::Seek;
		case avox::PlayerState::buffering: return EAvoxPlayerState::Buffering;
		case avox::PlayerState::stopped: return EAvoxPlayerState::Stopped;
		case avox::PlayerState::completed: return EAvoxPlayerState::Completed;
		default: return EAvoxPlayerState::None;
		}
	}
	UAvoxMediaPlayerComponent* owner = nullptr;
};

UAvoxMediaPlayerComponent::UAvoxMediaPlayerComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

UAvoxMediaPlayerComponent::~UAvoxMediaPlayerComponent()
{
	destroyPlayer();
}

void UAvoxMediaPlayerComponent::BeginPlay()
{
	Super::BeginPlay();
	if (bAutoPlay) Open(Url);
}

void UAvoxMediaPlayerComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	(void)EndPlayReason;
	destroyPlayer();
	Super::EndPlay(EndPlayReason);
}

void UAvoxMediaPlayerComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	flushEvents();
	updateTexture();
}

void UAvoxMediaPlayerComponent::createPlayer()
{
	if (mediaPlayer) return;
	mediaPlayer = avox::createMediaPlayer();
	playerOb = new FAvoxPlayerOb(this);
	avox::addMediaPlayerOb(mediaPlayer, playerOb);
	// 配置 (open前)
	mediaPlayer->setHardDecode(bHardDecode);
	if (ioPlan != EAvoxIoPlan::None)
	{
		mediaPlayer->setIoPlan(static_cast<avox::IoPlan>(static_cast<int32>(ioPlan)));
	}
	if (speed != 1.0)
	{
		mediaPlayer->speed(speed);
	}
	if (auto* audio = mediaPlayer->getAudioRender())
	{
		audio->setVolume(Volume);
	}
	// 视频帧桥接: 离屏surface + YUV输出, 帧进单帧槽位等游戏线程上传纹理
	videoBridge = new FAvoxVideoBridge();
	videoBridge->bindSurface(mediaPlayer->getSurfaceRender());
}

void UAvoxMediaPlayerComponent::destroyPlayer()
{
	if (!mediaPlayer) return;
	// 先解绑桥接再关播放器 (解绑需要 surface render 仍有效)
	if (videoBridge)
	{
		videoBridge->unbindSurface();
		delete videoBridge;
		videoBridge = nullptr;
	}
	mediaPlayer->close();
	avox::removeMediaPlayerOb(mediaPlayer, playerOb);
	delete playerOb;
	playerOb = nullptr;
	delete mediaPlayer;
	mediaPlayer = nullptr;
	stateCache.store((int)avox::PlayerState::none);
	FScopeLock lock(&eventMutex);
	pendingEvents.Reset();
	textureWidth = 0;
	textureHeight = 0;
}

void UAvoxMediaPlayerComponent::Open(const FString& InUrl)
{
	FString targetUrl = InUrl.Len() > 0 ? InUrl : Url;
	if (targetUrl.IsEmpty())
	{
		UE_LOG(LogAvoxPlayer, Warning, TEXT("Open: url 为空"));
		return;
	}
	Url = targetUrl;
	// 复用中的播放器直接重建, 规避流切换的状态残留
	destroyPlayer();
	createPlayer();
	stateCache.store((int)avox::PlayerState::opening);
	mediaPlayer->open(TCHAR_TO_UTF8(*Url));
}

void UAvoxMediaPlayerComponent::Close()
{
	destroyPlayer();
}

void UAvoxMediaPlayerComponent::Pause()
{
	if (mediaPlayer) mediaPlayer->pause();
}

void UAvoxMediaPlayerComponent::Resume()
{
	if (mediaPlayer) mediaPlayer->resume();
}

void UAvoxMediaPlayerComponent::Seek(int64 PositionMs)
{
	if (mediaPlayer) mediaPlayer->seek((int64_t)PositionMs);
}

void UAvoxMediaPlayerComponent::SetSpeed(double InSpeed)
{
	speed = InSpeed;
	if (mediaPlayer) mediaPlayer->speed(InSpeed);
}

void UAvoxMediaPlayerComponent::SetHardDecode(bool bEnable)
{
	bHardDecode = bEnable;
	if (mediaPlayer) mediaPlayer->setHardDecode(bEnable);
}

void UAvoxMediaPlayerComponent::SetVolume(float InVolume)
{
	Volume = FMath::Clamp(InVolume, 0.0f, 1.0f);
	if (mediaPlayer)
	{
		if (auto* audio = mediaPlayer->getAudioRender()) audio->setVolume(Volume);
	}
}

void UAvoxMediaPlayerComponent::SetIoPlan(EAvoxIoPlan Plan)
{
	ioPlan = Plan;
	if (mediaPlayer) mediaPlayer->setIoPlan(static_cast<avox::IoPlan>(static_cast<int32>(Plan)));
}

EAvoxPlayerState UAvoxMediaPlayerComponent::GetState() const
{
	switch (stateCache.load())
	{
	case 1: return EAvoxPlayerState::Opening;
	case 2: return EAvoxPlayerState::Ready;
	case 3: return EAvoxPlayerState::Playing;
	case 4: return EAvoxPlayerState::Pause;
	case 5: return EAvoxPlayerState::Seek;
	case 6: return EAvoxPlayerState::Buffering;
	case 7: return EAvoxPlayerState::Stopped;
	case 8: return EAvoxPlayerState::Completed;
	default: return EAvoxPlayerState::None;
	}
}

int64 UAvoxMediaPlayerComponent::GetDurationMs() const
{
	return mediaPlayer ? (int64)mediaPlayer->getDuration() : 0;
}

int64 UAvoxMediaPlayerComponent::GetPositionMs() const
{
	return mediaPlayer ? (int64)mediaPlayer->getPosition() : 0;
}

double UAvoxMediaPlayerComponent::GetProgress() const
{
	return mediaPlayer ? mediaPlayer->getProcess() : 0.0;
}

bool UAvoxMediaPlayerComponent::IsPlaying() const
{
	return GetState() == EAvoxPlayerState::Playing;
}

void UAvoxMediaPlayerComponent::handleStateChange(EAvoxPlayerState state)
{
	stateCache.store((int)state);
	FScopeLock lock(&eventMutex);
	FAvoxEvent e;
	e.type = FAvoxEvent::EType::State;
	e.state = state;
	pendingEvents.Add(MoveTemp(e));
}

void UAvoxMediaPlayerComponent::handleReady()
{
	FScopeLock lock(&eventMutex);
	FAvoxEvent e;
	e.type = FAvoxEvent::EType::Ready;
	pendingEvents.Add(MoveTemp(e));
}

void UAvoxMediaPlayerComponent::handleComplete()
{
	FScopeLock lock(&eventMutex);
	FAvoxEvent e;
	e.type = FAvoxEvent::EType::Complete;
	pendingEvents.Add(MoveTemp(e));
}

void UAvoxMediaPlayerComponent::handleError(int32 errorCode, const FString& message)
{
	UE_LOG(LogAvoxPlayer, Warning, TEXT("player error %d: %s"), errorCode, *message);
	FScopeLock lock(&eventMutex);
	FAvoxEvent e;
	e.type = FAvoxEvent::EType::Error;
	e.errorCode = errorCode;
	e.message = message;
	pendingEvents.Add(MoveTemp(e));
}

void UAvoxMediaPlayerComponent::flushEvents()
{
	TArray<FAvoxEvent> events;
	{
		FScopeLock lock(&eventMutex);
		events = MoveTemp(pendingEvents);
		pendingEvents.Reset();
	}
	for (FAvoxEvent& e : events)
	{
		switch (e.type)
		{
		case FAvoxEvent::EType::State: OnStateChanged.Broadcast(e.state); break;
		case FAvoxEvent::EType::Ready: OnReady.Broadcast(); break;
		case FAvoxEvent::EType::Complete: OnComplete.Broadcast(); break;
		case FAvoxEvent::EType::Error: OnError.Broadcast(e.errorCode, e.message); break;
		}
	}
}

void UAvoxMediaPlayerComponent::updateTexture()
{
	if (!videoBridge) return;
	TArray<uint8> bgra;
	int32 w = 0;
	int32 h = 0;
	if (!videoBridge->popFrame(bgra, w, h)) return;
	if (bgra.Num() != w * h * 4) return;
	// 分辨率变化(或首帧)重建纹理
	if (!VideoTexture || w != textureWidth || h != textureHeight)
	{
		const FName texName = MakeUniqueObjectName(GetOuter(), UTexture2D::StaticClass(), TEXT("AvoxVideoTexture"));
		VideoTexture = UTexture2D::CreateTransient(w, h, PF_B8G8R8A8, texName);
		if (!VideoTexture)
		{
			UE_LOG(LogAvoxPlayer, Error, TEXT("创建 %dx%d 视频纹理失败"), w, h);
			return;
		}
		VideoTexture->NeverStream = true;
		VideoTexture->SRGB = true;
		VideoTexture->UpdateResource();
		textureWidth = w;
		textureHeight = h;
		UE_LOG(LogAvoxPlayer, Log, TEXT("视频纹理就绪: %dx%d"), w, h);
	}
	// 异步上传: region/data 由渲染线程清理回调释放
	FUpdateTextureRegion2D* region = (FUpdateTextureRegion2D*)FMemory::Malloc(sizeof(FUpdateTextureRegion2D));
	*region = FUpdateTextureRegion2D(0, 0, 0, 0, w, h);
	uint8* upload = (uint8*)FMemory::Malloc((size_t)w * h * 4);
	FMemory::Memcpy(upload, bgra.GetData(), (size_t)w * h * 4);
	VideoTexture->UpdateTextureRegions(0, 1, region, w * 4, 4, upload,
		[](uint8* srcData, const FUpdateTextureRegion2D* regions)
		{
			FMemory::Free(srcData);
			FMemory::Free((void*)regions);
		});
}
