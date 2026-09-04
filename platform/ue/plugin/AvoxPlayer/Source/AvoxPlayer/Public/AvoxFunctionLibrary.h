#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "AvoxFunctionLibrary.generated.h"

UCLASS()
class AVOXPLAYER_API UAvoxFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// 本地文件路径转播放URL (转绝对路径, avox直接接受本地路径)
	UFUNCTION(BlueprintCallable, Category = "Avox")
	static FString MakeFileUrl(const FString& FilePath);

	// avox SDK 版本 (编译时头文件里的版本)
	UFUNCTION(BlueprintPure, Category = "Avox")
	static FString GetAvoxVersion();
};
