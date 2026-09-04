#pragma once

#include "CoreMinimal.h"

// 播放器状态, 对应 avox::PlayerState
UENUM(BlueprintType)
enum class EAvoxPlayerState : uint8
{
	None,
	Opening,
	Ready,
	Playing,
	Pause,
	Seek,
	Buffering,
	Stopped,
	Completed
};

// IO 方案, 对应 avox::IoPlan (下次 Open 生效)
UENUM(BlueprintType)
enum class EAvoxIoPlan : uint8
{
	None,
	Zlmediakit,
	Ffmpeg,
	Torrent
};
