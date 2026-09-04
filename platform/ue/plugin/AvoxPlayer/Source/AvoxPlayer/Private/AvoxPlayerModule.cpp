#pragma once

#include "CoreMinimal.h"
#include "AvoxPlayerModule.h"
#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY(LogAvoxPlayer);

void FAvoxPlayerModule::StartupModule()
{
#if PLATFORM_WINDOWS
	// 延迟加载前显式加载 avox.dll, 缺库时启动期给出明确错误而非首次调用时崩溃
	if (!FPlatformProcess::GetDllHandle(TEXT("avox.dll")))
	{
		UE_LOG(LogAvoxPlayer, Error,
			TEXT("avox.dll 加载失败: 请确认插件 Binaries/Win64 下存在 avox.dll 及其依赖 dll, 先在 avox 仓库运行 platform/ue/deploy_ue.ps1 部署"));
	}
#endif
}

IMPLEMENT_MODULE(FAvoxPlayerModule, AvoxPlayer)
