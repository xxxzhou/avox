#pragma once

#include "CoreMinimal.h"

DECLARE_LOG_CATEGORY_EXTERN(LogAvoxPlayer, Log, All);

class FAvoxPlayerModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
};
