// Copyright 2025 Just2Devs. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "ToolMenus.h"
#include "Modules/ModuleManager.h"

class FStructsHelperEditorModule : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;

private:
    TSharedRef<FExtender> OnExtendContentBrowserAssetSelectionMenu(const TArray<FAssetData>& SelectedAssets);
    void OnMenuExtension(FMenuBuilder& MenuBuilder, FAssetData SelectedAsset);

    // Refresh the struct break nodes in all blueprints in which the struct is used
    void OnRefreshStructs(FAssetData SelectedAsset);

    // Hide all disconnected pins on all the break nodes of a struct
    void OnHideDisconnectedPins(FAssetData SelectedAsset);

    // Generate list used properties for a struct
    void OnGetUnusedProperties(FAssetData SelectedAsset);
    
    void OnFindPropertyReferences(FAssetData SelectedAsset);
    FReply OnSearchPropertyReference(UScriptStruct* ScriptStruct);
    
    UBlueprint* GetBlueprintFromAsset(const FAssetData& Asset);
    ULevelScriptBlueprint* GetLevelScriptBlueprint(UObject* Asset);
    bool IsAssetLevel(UObject* Asset);
    void OpenAssetEditor(UObject* Asset, UEdGraphPin* FocusPin);
    
private:
    TArray<TSharedPtr<FString>> Properties;
    TSharedPtr<STextComboBox> PropertiesComboBox;
    TSharedPtr<SWindow> FindPropertyReferencesWindow;
    TSharedPtr<SWindow> ReferencesWindow;
    TMap<FAssetData, UEdGraphPin*> Dependencies;
    
};
