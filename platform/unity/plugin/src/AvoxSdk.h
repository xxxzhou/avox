#pragma once

// avox SDK 头统一入口: 屏蔽 avox 头在严格告警下的告警升级 (同 platform/ue 插件)
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4100 4127 4244 4245 4267 4324 4456 4457 4458 4459 4702 5045)
#endif

#include "avox/AvoxCore.h"
#include "avox/AvoxLayer.h"
#include "avox/AvoxImage.h"
#include "avox/AvoxVersion.h"

#ifdef _MSC_VER
#pragma warning(pop)
#endif
