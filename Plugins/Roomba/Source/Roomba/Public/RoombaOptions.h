#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "RoombaOptions.generated.h"

UCLASS(BlueprintType, config = EditorPerProjectUserSettings)
class ROOMBA_API URoombaOptions : public UObject
{
    GENERATED_BODY()

public:
    URoombaOptions() {}

    // Folder picker property
    UPROPERTY(EditAnywhere, Category = "Folder Picker", meta = (ContentDir))
    FDirectoryPath SelectedFolder;

    // Resolved path to use in code
    FString GetResolvedFolderPath() const
    {
        if (SelectedFolder.Path.IsEmpty())
        {
            return FPaths::ProjectContentDir(); // Default to Content folder
        }
        return FPaths::ConvertRelativePathToFull(SelectedFolder.Path);
    }
};
