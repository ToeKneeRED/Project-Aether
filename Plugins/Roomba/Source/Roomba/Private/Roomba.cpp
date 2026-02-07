#include "Roomba.h"
#include "Modules/ModuleManager.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "ObjectTools.h"
#include "LevelEditor.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "ContentBrowserModule.h"
#include "Misc/ScopedSlowTask.h"
#include "RoombaOptions.h"
#include "ISettingsModule.h"

#include "Engine/StaticMesh.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "Editor.h"
#include "GameFramework/Actor.h"

#include "Editor/EditorEngine.h"
#include "UnrealEdGlobals.h"

#include "Widgets/Layout/SBorder.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "Widgets/Text/STextBlock.h"
#include "Editor/ContentBrowser/Public/ContentBrowserDelegates.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"

#include "Widgets/Input/SFilePathPicker.h"

#include "PropertyCustomizationHelpers.h"

IMPLEMENT_MODULE(FRoombaModule, Roomba)

void FRoombaModule::StartupModule()
{
    UE_LOG(LogTemp, Warning, TEXT("Roomba Plugin Loaded!"));

    FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");

    TSharedRef<FExtender> Extender = MakeShared<FExtender>();

    Extender->AddToolBarExtension(
        "Content",
        EExtensionHook::After,
        nullptr,
        FToolBarExtensionDelegate::CreateLambda([](FToolBarBuilder& Builder)
            {
                Builder.AddToolBarButton(
                    FUIAction(
                        FExecuteAction::CreateLambda([]()
                            {
                                ShowPathPicker([](const FString& SelectedPath)
                                    {
                                        if (SelectedPath.IsEmpty())
                                        {
                                            UE_LOG(LogTemp, Warning, TEXT("No folder selected."));
                                            return;
                                        }

                                        UE_LOG(LogTemp, Log, TEXT("Selected Folder: %s"), *SelectedPath);

                                        // Call the cleanup function with the selected path
                                        DeleteUnusedMeshes(SelectedPath);
                                    });
                            })

                    ),
                    NAME_None,
                    FText::FromString("Roomba Clean"),
                    FText::FromString("Clean up unused static meshes"),
                    FSlateIcon(FAppStyle::GetAppStyleSetName(), "ClassIcon.StaticMesh")
                );
            })
    );

    LevelEditorModule.GetToolBarExtensibilityManager()->AddExtender(Extender);
}

void FRoombaModule::ShutdownModule()
{
    ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings");
    if (SettingsModule)
    {
        SettingsModule->UnregisterSettings("Project", "Plugins", "Roomba");
    }

    UE_LOG(LogTemp, Warning, TEXT("Roomba Plugin Unloaded!"));
}

void GetAllStaticMeshesInFolder(const FString& FolderPath, TArray<FAssetData>& OutAssets)
{
    FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
    FARFilter Filter;
    Filter.PackagePaths.Add(*FolderPath);
    Filter.ClassPaths.Add(UStaticMesh::StaticClass()->GetClassPathName());
    Filter.bRecursivePaths = true;

    AssetRegistryModule.Get().GetAssets(Filter, OutAssets);
}

#if WITH_EDITOR
void GetUsedStaticMeshesInLevel(TSet<UStaticMesh*>& OutUsedMeshes)
{
    UWorld* World = GEditor->GetEditorWorldContext().World();
    if (!World) return;

    for (AActor* Actor : World->GetCurrentLevel()->Actors)
    {
        if (!Actor) continue;

        TArray<UStaticMeshComponent*> Components;
        Actor->GetComponents<UStaticMeshComponent>(Components);

        for (UStaticMeshComponent* Component : Components)
        {
            if (Component && Component->GetStaticMesh())
            {
                OutUsedMeshes.Add(Component->GetStaticMesh());
            }
        }
    }
}
#endif

void DeleteUnusedMeshes(const FString& TargetFolder)
{
    if (TargetFolder.IsEmpty())
    {
        UE_LOG(LogTemp, Error, TEXT("No target folder provided for Roomba cleanup."));
        return;
    }

    TArray<FAssetData> AllMeshes;
    GetAllStaticMeshesInFolder(TargetFolder, AllMeshes);

    if (AllMeshes.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("No static meshes found in folder: %s"), *TargetFolder);
        return;
    }

    TSet<UStaticMesh*> UsedMeshes;
    GetUsedStaticMeshesInLevel(UsedMeshes);

    FScopedSlowTask SlowTask(AllMeshes.Num(), FText::FromString(TEXT("Cleaning unused meshes...")));
    SlowTask.MakeDialog(true);

    for (const FAssetData& AssetData : AllMeshes)
    {
        SlowTask.EnterProgressFrame(1);

        UStaticMesh* Mesh = Cast<UStaticMesh>(AssetData.GetAsset());
        if (Mesh && !UsedMeshes.Contains(Mesh))
        {
            UE_LOG(LogTemp, Warning, TEXT("Deleting Unused Mesh: %s"), *Mesh->GetName());
            ObjectTools::DeleteAssets({ Mesh }, false);
        }
    }

    UE_LOG(LogTemp, Log, TEXT("Roomba cleaning completed."));
}

void ShowPathPicker(const TFunction<void(const FString&)>& OnPathSelectedCallback)
{
    // Create the folder picker options object
    URoombaOptions* FolderPicker = NewObject<URoombaOptions>();
    FolderPicker->AddToRoot(); // Prevent garbage collection

    // Create a details view for the folder picker
    FPropertyEditorModule& PropertyEditorModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
    FDetailsViewArgs DetailsViewArgs;
    DetailsViewArgs.bAllowSearch = false;
    DetailsViewArgs.bShowOptions = false;
    DetailsViewArgs.bShowPropertyMatrixButton = false;

    TSharedRef<IDetailsView> DetailsView = PropertyEditorModule.CreateDetailView(DetailsViewArgs);
    DetailsView->SetObject(FolderPicker);

    // Create a modal window with the details view
    TSharedRef<SWindow> PickerWindow = SNew(SWindow)
        .Title(FText::FromString("Select Folder"))
        .ClientSize(FVector2D(400, 200))
        .SupportsMinimize(false)
        .SupportsMaximize(false);

    PickerWindow->SetContent(
        SNew(SVerticalBox)
        + SVerticalBox::Slot()
        .FillHeight(1.0f)
        .Padding(5)
        [
            DetailsView
        ]
        + SVerticalBox::Slot()
        .AutoHeight()
        .HAlign(HAlign_Right)
        .Padding(10)
        [
            SNew(SButton)
                .Text(FText::FromString("OK"))
                .OnClicked_Lambda([PickerWindow, FolderPicker, OnPathSelectedCallback]()
                    {
                        FString SelectedPath = FolderPicker->GetResolvedFolderPath();
                        if (!SelectedPath.IsEmpty())
                        {
                            OnPathSelectedCallback(SelectedPath);
                        }
                        PickerWindow->RequestDestroyWindow();
                        return FReply::Handled();
                    })
        ]
        + SVerticalBox::Slot()
        .AutoHeight()
        .HAlign(HAlign_Right)
        .Padding(10)
        [
            SNew(SButton)
                .Text(FText::FromString("Cancel"))
                .OnClicked_Lambda([PickerWindow]()
                    {
                        PickerWindow->RequestDestroyWindow();
                        return FReply::Handled();
                    })
        ]
    );

    FSlateApplication::Get().AddWindow(PickerWindow);
}