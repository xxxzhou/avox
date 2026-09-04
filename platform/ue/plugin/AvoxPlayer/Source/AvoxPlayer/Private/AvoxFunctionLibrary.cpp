#include "AvoxFunctionLibrary.h"
#include "AvoxSdk.h"
#include "Misc/Paths.h"

FString UAvoxFunctionLibrary::MakeFileUrl(const FString& FilePath)
{
	return FPaths::ConvertRelativePathToFull(FilePath);
}

FString UAvoxFunctionLibrary::GetAvoxVersion()
{
	return FString::Printf(TEXT("%s (%s)"), TEXT(AVOX_COMMIT_VERSION), TEXT(AVOX_COMMIT_HASH));
}
