#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FRoombaModule : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;
};

void GetAllStaticMeshesInFolder(const FString& FolderPath, TArray<FAssetData>& OutAssets);
void GetUsedStaticMeshesInLevel(TSet<UStaticMesh*>& OutUsedMeshes);
void DeleteUnusedMeshes(const FString& TargetFolder);
void ShowPathPicker(const TFunction<void(const FString&)>& OnPathSelectedCallback);