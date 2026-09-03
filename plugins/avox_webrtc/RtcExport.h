#pragma once

#include "avox/AvoxPlayer.h"

namespace avox {

// IRtcPlayer / RtcRollType 已移至 avox/AvoxPlayer.h (核心层, SWIG 友好)
// createWebRtcPlayer / createZlTestSdpAgent / addRtcPlayerOb / removeRtcPlayerOb
// 均已移至 avox/AvoxPlayer.h (核心层 AVOX_EXPORT)
// addRtcPlayerOb/removeRtcPlayerOb 通过 dynamic_cast<BasePlayer*> cross-cast 实现,
// 核心层无需知道 RtcPlayer 具体类

}
