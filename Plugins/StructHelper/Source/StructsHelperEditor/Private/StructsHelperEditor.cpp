// Copyright 2025 Just2Devs. All Rights Reserved.

#include "StructsHelperEditor.h"
#include "Engine/LevelScriptBlueprint.h"
#include "BlueprintEditorModule.h"
#include "ContentBrowserModule.h"
#include "K2Node_BreakStruct.h"
#include "K2Node_SetFieldsInStruct.h"
#include "K2Node_Variable.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Widgets/Input/SHyperlink.h"
#include "Widgets/Input/STextComboBox.h"

#define LOCTEXT_NAMESPACE "FStructsHelperEditorModule"

DEFINE_LOG_CATEGORY_STATIC(LogStructHelpers, Log, Log)

void FStructsHelperEditorModule::StartupModule()
{
	FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
	TArray<FContentBrowserMenuExtender_SelectedAssets>& CBMenuExtenderDelegates = ContentBrowserModule.GetAllAssetViewContextMenuExtenders();
	CBMenuExtenderDelegates.Add(FContentBrowserMenuExtender_SelectedAssets::CreateRaw(this, &FStructsHelperEditorModule::OnExtendContentBrowserAssetSelectionMenu));
}

void FStructsHelperEditorModule::ShutdownModule()
{
}

TSharedRef<FExtender> FStructsHelperEditorModule::OnExtendContentBrowserAssetSelectionMenu(const TArray<FAssetData>& SelectedAssets)
{
	TSharedRef<FExtender> Extender = MakeShared<FExtender>();

	if (SelectedAssets.Num() == 1 && Cast<UStruct>(SelectedAssets[0].GetAsset()))
	{
		FUIAction RefreshStructsAction(FExecuteAction::CreateRaw(this, &FStructsHelperEditorModule::OnRefreshStructs, SelectedAssets[0]));
		Extender->AddMenuExtension("GetAssetActions", EExtensionHook::After, nullptr, FMenuExtensionDelegate::CreateRaw(this, &FStructsHelperEditorModule::OnMenuExtension, SelectedAssets[0]));
	}

	return Extender;
}

void FStructsHelperEditorModule::OnMenuExtension(FMenuBuilder& MenuBuilder, FAssetData SelectedAsset)
{
	MenuBuilder.AddSubMenu(
		LOCTEXT("StructsHelperMenuTitle", "Structs Helper"),
		LOCTEXT("StructsHelperTooltip", "Collection of helper functionalities for structs"),
		FNewMenuDelegate::CreateLambda([SelectedAsset, this](FMenuBuilder& SubMenuBuilder)
		{
			SubMenuBuilder.AddMenuEntry(
				LOCTEXT("StructsHelper_RefreshBlueprints", "Refresh References"),
				LOCTEXT("StructsHelper_RefreshBlueprintsTooltip", "Refreshes all references to this struct in blueprint assets"),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateRaw(this, &FStructsHelperEditorModule::OnRefreshStructs, SelectedAsset))
			);

			SubMenuBuilder.AddMenuEntry(
				LOCTEXT("StructsHelper_HideDisconnectedPins", "Hide Disconnected Pins"),
				LOCTEXT("StructsHelper_HideDisconnectedPinsTooltip", "Hides all disconnected pins on break nodes"),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateRaw(this, &FStructsHelperEditorModule::OnHideDisconnectedPins, SelectedAsset))
			);

			SubMenuBuilder.AddMenuEntry(
				LOCTEXT("StructsHelper_GetUnusedProperties", "Get Unused Properties"),
				LOCTEXT("StructsHelper_GetUnusedPropertiesTooltip", "Shows a list of unused properties"),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateRaw(this, &FStructsHelperEditorModule::OnGetUnusedProperties, SelectedAsset))
			);

			SubMenuBuilder.AddMenuEntry(
				LOCTEXT("StructsHelper_FindPropertyReferences", "Find Property References"),
				LOCTEXT("Structs_Helper_FindPropertyReferencesTooltip", "Find all the asset references to a specific struct property"),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateRaw(this, &FStructsHelperEditorModule::OnFindPropertyReferences, SelectedAsset))
			);
		}), false, FSlateIcon(FAppStyle::GetAppStyleSetName(), "ClassIcon.UserDefinedStruct"));
}

void FStructsHelperEditorModule::OnRefreshStructs(FAssetData SelectedAsset)
{
	if (!SelectedAsset.GetAsset())
	{
		UE_LOG(LogStructHelpers, Error, TEXT("Selected asset is not valid"));
		return;
	}

	UScriptStruct* ScriptStruct = Cast<UScriptStruct>(SelectedAsset.GetAsset());
	if (!ScriptStruct)
	{
		UE_LOG(LogStructHelpers, Error, TEXT("Selected asset is not a script struct"));
		return;
	}

	const FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));

	TArray<FAssetIdentifier> References;
	AssetRegistryModule.GetRegistry().GetReferencers(ScriptStruct->GetPackage()->GetFName(), References);

	UE_LOG(LogStructHelpers, Log, TEXT("Searched for %s references and found %i"), *ScriptStruct->GetName(), References.Num());

	for (FAssetIdentifier Reference : References)
	{
		TArray<FAssetData> Assets;
		AssetRegistryModule.GetRegistry().GetAssetsByPackageName(Reference.PackageName, Assets);

		UE_LOG(LogStructHelpers, Log, TEXT("Found %i assets using reference %s"), Assets.Num(), *Reference.PackageName.ToString());

		for (const FAssetData& Asset : Assets)
		{
			if (!Asset.GetAsset())
			{
				UE_LOG(LogStructHelpers, Warning, TEXT("Referenced asset is not valid"));
				continue;
			}

			if (!Asset.PackagePath.ToString().StartsWith("/Game"))
			{
				UE_LOG(LogStructHelpers, Log, TEXT("Asset is not in project folder skipping"));
				continue;
			}

			UBlueprint* Blueprint = GetBlueprintFromAsset(Asset);
			if(!Blueprint)
			{
				continue;
			}
			
			TArray<UEdGraph*> Graphs;
			Blueprint->GetAllGraphs(Graphs);

			bool bRefreshedNodes = false;
			for (UEdGraph* Graph : Graphs)
			{
				TArray<UK2Node_BreakStruct*> BreakNodes;
				Graph->GetNodesOfClass<UK2Node_BreakStruct>(BreakNodes);
				for (UK2Node_BreakStruct* Node : BreakNodes)
				{
					if (Node->StructType == ScriptStruct)
					{
						const bool bCurDisableOrphanSaving = Node->bDisableOrphanPinSaving;
						Node->bDisableOrphanPinSaving = true;
						const UEdGraphSchema* Schema = Graph->GetSchema();
						Schema->ReconstructNode(*Node, true);
						Node->ClearCompilerMessage();
						Node->bDisableOrphanPinSaving = bCurDisableOrphanSaving;
						bRefreshedNodes = true;
					}
				}
				
				TArray<UK2Node_SetFieldsInStruct*> SetNodes;
				Graph->GetNodesOfClass<UK2Node_SetFieldsInStruct>(SetNodes);
				for (UK2Node_SetFieldsInStruct* Node : SetNodes)
				{
					if (Node->StructType == ScriptStruct)
					{
						const bool bCurDisableOrphanSaving = Node->bDisableOrphanPinSaving;
						Node->bDisableOrphanPinSaving = true;
						const UEdGraphSchema* Schema = Graph->GetSchema();
						Schema->ReconstructNode(*Node, true);
						Node->ClearCompilerMessage();
						Node->bDisableOrphanPinSaving = bCurDisableOrphanSaving;
						bRefreshedNodes = true;
					}
				}
			}

			if (bRefreshedNodes)
			{
				FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
			}
		}
	}
}

void FStructsHelperEditorModule::OnHideDisconnectedPins(FAssetData SelectedAsset)
{
	if (!SelectedAsset.GetAsset())
	{
		return;
	}

	UScriptStruct* ScriptStruct = Cast<UScriptStruct>(SelectedAsset.GetAsset());
	if (!ScriptStruct)
	{
		return;
	}

	const FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));

	TArray<FAssetIdentifier> References;
	AssetRegistryModule.GetRegistry().GetReferencers(ScriptStruct->GetPackage()->GetFName(), References);

	for (FAssetIdentifier Reference : References)
	{
		TArray<FAssetData> Assets;
		AssetRegistryModule.GetRegistry().GetAssetsByPackageName(Reference.PackageName, Assets);
		for (const FAssetData& Asset : Assets)
		{
			if (!Asset.GetAsset())
			{
				continue;
			}

			if (!Asset.PackagePath.ToString().StartsWith("/Game"))
			{
				continue;
			}

			UBlueprint* Blueprint = GetBlueprintFromAsset(Asset);
			if (!Blueprint)
			{
				continue;
			}
			
			TArray<UEdGraph*> Graphs;
			Blueprint->GetAllGraphs(Graphs);

			bool bRefreshedNodes = false;
			for (UEdGraph* Graph : Graphs)
			{
				TArray<UK2Node_BreakStruct*> Nodes;
				Graph->GetNodesOfClass<UK2Node_BreakStruct>(Nodes);
				for (UK2Node_BreakStruct* Node : Nodes)
				{
					if (Node->StructType == ScriptStruct)
					{
						bool bNodeModified = false;
						for (int32 i = 0; i < Node->ShowPinForProperties.Num(); i++)
						{
							UEdGraphPin* Pin = Node->FindPin(Node->ShowPinForProperties[i].PropertyName, EGPD_Output);
							if (Pin && Pin->LinkedTo.Num() == 0)
							{
								Node->ShowPinForProperties[i].bShowPin = false;
								bNodeModified = true;
								bRefreshedNodes = true;
							}
						}

						if (bNodeModified)
						{
							Node->ReconstructNode();
						}
					}
				}
			}

			if (bRefreshedNodes)
			{
				FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
			}
		}
	}
}

void FStructsHelperEditorModule::OnGetUnusedProperties(FAssetData SelectedAsset)
{
	if (!SelectedAsset.GetAsset())
	{
		return;
	}

	UScriptStruct* ScriptStruct = Cast<UScriptStruct>(SelectedAsset.GetAsset());
	if (!ScriptStruct)
	{
		return;
	}

	const FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	
	TArray<FAssetData> DataTableAssets;
	AssetRegistryModule.GetRegistry().GetAssetsByClass(UDataTable::StaticClass()->GetClassPathName(), DataTableAssets);

	for (FAssetData Asset : DataTableAssets)
	{
		if (!Asset.GetAsset())
		{
			continue;
		}

		if (!Asset.PackagePath.ToString().StartsWith("/Game"))
		{
			continue;
		}
			
		if (UDataTable* DataTable = Cast<UDataTable>(Asset.GetAsset()))
		{
			FString StructName = DataTable->GetRowStruct()->GetStructPathName().GetAssetName().ToString();
			if (ScriptStruct->GetStructPathName().GetAssetName().ToString() == StructName && !DataTable->GetRowNames().IsEmpty())
			{
				FMessageDialog::Open(EAppMsgCategory::Info, EAppMsgType::Ok, EAppReturnType::Ok, LOCTEXT("NoPropertiesFound_Text", "All properties are used"), LOCTEXT("NoPropertiesFoundTitle_Text", "No properties found"));
				return;
			}
		}
	}
	
	struct FPropertyDescription
	{
	public:
		FString Name;
		FString Type;
		bool bUsed = false;
	};

	TArray<FPropertyDescription> PropertyDescriptions;

	for (TFieldIterator<FProperty> It(ScriptStruct); It; ++It)
	{
		FProperty* Property = *It;
		FString Type;

		if (CastField<FFloatProperty>(Property) || CastField<FDoubleProperty>(Property))
		{
			Type = "Float";
		}

		if (CastField<FStrProperty>(Property))
		{
			Type = "String";
		}

		if (CastField<FBoolProperty>(Property))
		{
			Type = "Bool";
		}

		if (CastField<FNameProperty>(Property))
		{
			Type = "Name";
		}

		if (CastField<FTextProperty>(Property))
		{
			Type = "Text";
		}

		if (CastField<FByteProperty>(Property))
		{
			Type = "Byte";
		}

		if (CastField<FIntProperty>(Property))
		{
			Type = "Int";
		}

		if (CastField<FInt64Property>(Property))
		{
			Type = "Int64";
		}

		if (FStructProperty* StructProperty = CastField<FStructProperty>(Property))
		{
			Type = StructProperty->Struct->GetStructPathName().GetAssetName().ToString();
		}

		PropertyDescriptions.Add({Property->GetDisplayNameText().ToString(), Type, false});
	}

	TArray<FAssetIdentifier> References;
	AssetRegistryModule.GetRegistry().GetReferencers(ScriptStruct->GetPackage()->GetFName(), References);

	for (FAssetIdentifier Reference : References)
	{
		TArray<FAssetData> Assets;
		AssetRegistryModule.GetRegistry().GetAssetsByPackageName(Reference.PackageName, Assets);
		for (const FAssetData& Asset : Assets)
		{
			if (!Asset.GetAsset())
			{
				continue;
			}

			if (!Asset.PackagePath.ToString().StartsWith("/Game"))
			{
				continue;
			}
			
			UBlueprint* Blueprint = GetBlueprintFromAsset(Asset);
			if (!Blueprint)
			{
				continue;
			}
			
			TArray<UEdGraph*> Graphs;
			Blueprint->GetAllGraphs(Graphs);

			for (UEdGraph* Graph : Graphs)
			{
				TArray<UK2Node_BreakStruct*> BreakNodes;
				Graph->GetNodesOfClass<UK2Node_BreakStruct>(BreakNodes);
				for (UK2Node_BreakStruct* Node : BreakNodes)
				{
					if (Node->StructType != ScriptStruct)
					{
						continue;
					}
					
					for (UEdGraphPin* Pin : Node->GetAllPins())
					{
						for (auto& Property : PropertyDescriptions)
						{
							if (Pin->GetName().StartsWith(Property.Name) && Pin->LinkedTo.Num() > 0)
							{
								Property.bUsed = true;
							}
						}
					}
				}
				
				TArray<UK2Node_SetFieldsInStruct*> SetNodes;
				Graph->GetNodesOfClass<UK2Node_SetFieldsInStruct>(SetNodes);
				for (UK2Node_SetFieldsInStruct* Node : SetNodes)
				{
					if (Node->StructType != ScriptStruct)
					{
						continue;
					}
						
					for (int32 i = 0; i < Node->ShowPinForProperties.Num(); i++)
					{
						if (!Node->ShowPinForProperties[i].bShowPin)
						{
							continue;
						}
						
						UEdGraphPin* Pin = Node->FindPin(Node->ShowPinForProperties[i].PropertyName, EGPD_Input);
						for (auto& Property : PropertyDescriptions)
						{
							if (Pin->GetName().StartsWith(Property.Name))
							{
								Property.bUsed = true;
							}
						}
					}
				}
			}
		}
	}

	FString UnusedProperties;
	for (auto Property : PropertyDescriptions)
	{
		if (!Property.bUsed)
		{
			UnusedProperties += Property.Type + ": " + Property.Name;
			UnusedProperties += LINE_TERMINATOR;
		}
	}

	if (UnusedProperties.IsEmpty())
	{
		FMessageDialog::Open(EAppMsgCategory::Info, EAppMsgType::Ok, EAppReturnType::Ok, LOCTEXT("NoPropertiesFound_Text", "All properties are used"), LOCTEXT("NoPropertiesFoundTitle_Text", "No properties found"));
	}
	else
	{
		FMessageDialog::Open(EAppMsgCategory::Info, EAppMsgType::Ok, EAppReturnType::Ok, FText::FromString(UnusedProperties), LOCTEXT("UnusedPropertiesTitle_Text", "Struct Unused Properties"));
	}
}

void FStructsHelperEditorModule::OnFindPropertyReferences(FAssetData SelectedAsset)
{
	UScriptStruct* ScriptStruct = Cast<UScriptStruct>(SelectedAsset.GetAsset());
	if (!ScriptStruct)
	{
		return;
	}

	Properties.Empty();
	for (TFieldIterator<FProperty> It(ScriptStruct); It; ++It)
	{
		FProperty* Property = *It;
		TSharedPtr<FString> PropertyName = MakeShared<FString>(Property->GetDisplayNameText().ToString());
		Properties.Add(PropertyName);
	}

	SAssignNew(FindPropertyReferencesWindow, SWindow)
	.Title(LOCTEXT("StructHelpers_FindPropertyReferencesWindow", "Select Property"))
	.SizingRule(ESizingRule::FixedSize)
	.ClientSize(FVector2d(300, 100))
	.SupportsMinimize(false)
	.SupportsMaximize(false)
	.AutoCenter(EAutoCenter::PrimaryWorkArea)
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.Padding(10, 10, 10, 0)
		.SizeParam(FAuto())
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.Padding(10)
			.SizeParam(FAuto())
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("StructsHelper_FindPropertyReferencesPropertyName", "Property"))
			]
			+ SHorizontalBox::Slot()
			.Padding(10)
			.SizeParam(FStretch())
			.VAlign(VAlign_Center)
			[
				SAssignNew(PropertiesComboBox, STextComboBox)
				.OptionsSource(&Properties)
				.InitiallySelectedItem(Properties[0])
			]
		]
		+ SVerticalBox::Slot()
		.Padding(20, 10)
		.HAlign(HAlign_Right)
		.SizeParam(FAuto())
		[
			SNew(SButton)
			.HAlign(HAlign_Right)
			.OnClicked_Raw(this, &FStructsHelperEditorModule::OnSearchPropertyReference, ScriptStruct)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("StructsHelper_FindPropertyReferencesFindButton", "Search"))
			]
		]
	];

	FSlateApplication::Get().AddModalWindow(FindPropertyReferencesWindow.ToSharedRef(), nullptr);
}

FReply FStructsHelperEditorModule::OnSearchPropertyReference(UScriptStruct* ScriptStruct)
{
	TSharedPtr<FString> SelectedProperty = PropertiesComboBox->GetSelectedItem();

	Dependencies.Empty();

	TArray<FAssetIdentifier> References;

	const FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	
	TArray<FAssetData> OutAssetData;
	AssetRegistryModule.GetRegistry().GetAssetsByClass(UDataTable::StaticClass()->GetClassPathName(), OutAssetData);
	for (const FAssetData& Asset: OutAssetData)
	{
		if (!Asset.GetAsset())
		{
			continue;
		}

		if (!Asset.PackagePath.ToString().StartsWith("/Game"))
		{
			continue;
		}
			
		if (const UDataTable* DataTable = Cast<UDataTable>(Asset.GetAsset()))
		{
			if (ScriptStruct->GetStructPathName().GetAssetName().ToString() == DataTable->GetRowStruct()->GetStructPathName().GetAssetName().ToString() && !DataTable->GetRowNames().IsEmpty())
			{
				Dependencies.Add(Asset, nullptr);
			}
		}
	}
	
	AssetRegistryModule.GetRegistry().GetReferencers(ScriptStruct->GetPackage()->GetFName(), References);

	for (FAssetIdentifier Reference : References)
	{
		TArray<FAssetData> Assets;
		AssetRegistryModule.GetRegistry().GetAssetsByPackageName(Reference.PackageName, Assets);
		for (const FAssetData& Asset : Assets)
		{
			if (!Asset.GetAsset())
			{
				continue;
			}

			if (!Asset.PackagePath.ToString().StartsWith("/Game"))
			{
				continue;
			}

			UBlueprint* Blueprint = GetBlueprintFromAsset(Asset);
			if (!Blueprint)
			{
				continue;
			}
			
			TArray<UEdGraph*> Graphs;
			Blueprint->GetAllGraphs(Graphs);

			for (const UEdGraph* Graph : Graphs)
			{
				TArray<UK2Node_BreakStruct*> BreakNodes;
				Graph->GetNodesOfClass<UK2Node_BreakStruct>(BreakNodes);
				for (const UK2Node_BreakStruct* Node : BreakNodes)
				{
					if (Node->StructType != ScriptStruct)
					{
						continue;
					}
					
					for (UEdGraphPin* Pin : Node->GetAllPins())
					{
						if (Pin->GetName().StartsWith(*SelectedProperty) && Pin->LinkedTo.Num() > 0)
						{
							Dependencies.Add(Asset, Pin);
						}
					}
				}
				
				TArray<UK2Node_SetFieldsInStruct*> SetNodes;
				Graph->GetNodesOfClass<UK2Node_SetFieldsInStruct>(SetNodes);
				for (const UK2Node_SetFieldsInStruct* Node : SetNodes)
				{
					if (Node->StructType != ScriptStruct)
					{
						continue;
					}
					
					for (int32 i = 0; i < Node->ShowPinForProperties.Num(); i++)
					{
						if (!Node->ShowPinForProperties[i].bShowPin)
						{
							continue;
						}
						
						UEdGraphPin* Pin = Node->FindPin(Node->ShowPinForProperties[i].PropertyName, EGPD_Input);
						if (Pin->GetName().StartsWith(*SelectedProperty))
						{
							Dependencies.Add(Asset, Pin);
						}
					}
				}
			}
		}
	}
	
	FindPropertyReferencesWindow->RequestDestroyWindow();

	if (Dependencies.IsEmpty())
	{
		FMessageDialog::Open(EAppMsgCategory::Info, EAppMsgType::Ok, EAppReturnType::Ok, LOCTEXT("Results_Text", "No reference"), LOCTEXT("ResultsTitle_Text", "Results"));
		return FReply::Handled();
	}

	TSharedPtr<SVerticalBox> Links = SNew(SVerticalBox);
	for (auto& Dependency : Dependencies)
	{
		Links->AddSlot()
		     .AutoHeight()
		     .HAlign(HAlign_Left)
		[
			SNew(SHyperlink)
			.Text(FText::FromString(Dependency.Key.AssetName.ToString()))
			.ToolTipText(FText::Format(LOCTEXT("AssetHyperlinkTooltipFormat", "Open asset '{0}'"), FText::FromString(Dependency.Key.AssetName.ToString())))
			.OnNavigate_Lambda([Dependency, this]()
			{
				ReferencesWindow->RequestDestroyWindow();

				UObject* Asset = Dependency.Key.GetAsset();
				if (Asset != nullptr)
				{
					OpenAssetEditor(Asset, Dependency.Value);
				}
			})
		];
	}

	ReferencesWindow = SNew(SWindow)
		.Title(LOCTEXT("StructHelpers_FindPropertyReferencesWindowResults", "Results"))
		.ClientSize(FVector2d(300, 150))
		.SizingRule(ESizingRule::FixedSize)
		.SupportsMinimize(false)
		.SupportsMaximize(false)
		.AutoCenter(EAutoCenter::PrimaryWorkArea)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			[
				SNew(SScrollBox)
				+ SScrollBox::Slot()
				.Padding(10)
				[
					Links.ToSharedRef()
				]
			]
			+ SVerticalBox::Slot()
			.VAlign(VAlign_Center)
			.AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.HAlign(HAlign_Right)
				.VAlign(VAlign_Center)
				[
					SNew(SButton)
					.Text(FText::FromString("Open Assets"))
					.OnClicked_Lambda([this]()
					{
						ReferencesWindow->RequestDestroyWindow();

						for (auto& Dependency : Dependencies)
						{
							UObject* Asset = Dependency.Key.GetAsset();
							if (Asset != nullptr)
							{
								OpenAssetEditor(Asset, Dependency.Value);
							}
						}

						return FReply::Handled();
					})
				]
				+ SHorizontalBox::Slot()
				.HAlign(HAlign_Right)
				.VAlign(VAlign_Center)
				.AutoWidth()
				.Padding(10.0f, 5.0f)
				[
					SNew(SButton)
					.Text(FText::FromString("Close"))
					.OnClicked_Lambda([this]()
					{
						ReferencesWindow->RequestDestroyWindow();
						return FReply::Handled();
					})
				]
			]
		];

	FSlateApplication::Get().AddModalWindow(ReferencesWindow.ToSharedRef(), nullptr);

	return FReply::Handled();
}

UBlueprint* FStructsHelperEditorModule::GetBlueprintFromAsset(const FAssetData& Asset)
{
	UBlueprint* Blueprint = Cast<UBlueprint>(Asset.GetAsset());
	if (!Blueprint)
	{
		UWorld* Level = Cast<UWorld>(Asset.GetAsset());
		if (Level && Level->PersistentLevel)
		{
			Blueprint = Cast<UBlueprint>(Level->PersistentLevel->GetLevelScriptBlueprint(true));
		}
	}
	
	return Blueprint;
}

ULevelScriptBlueprint* FStructsHelperEditorModule::GetLevelScriptBlueprint(UObject* Asset)
{
	UWorld* Level = Cast<UWorld>(Asset);
	if (Level && Level->PersistentLevel)
	{
		return Level->PersistentLevel->GetLevelScriptBlueprint(true);
	}
	
	return nullptr;
}

bool FStructsHelperEditorModule::IsAssetLevel(UObject* Asset)
{
	return Cast<UWorld>(Asset) != nullptr;
}

void FStructsHelperEditorModule::OpenAssetEditor(UObject* Asset, UEdGraphPin* FocusPin)
{
	if (IsAssetLevel(Asset))
	{
		if (ULevelScriptBlueprint* LevelScriptBlueprint = GetLevelScriptBlueprint(Asset))
		{
			if (UAssetEditorSubsystem* AssetEditor = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
			{
				AssetEditor->OpenEditorForAsset(LevelScriptBlueprint);
				if (IBlueprintEditor* BlueprintEditor = (IBlueprintEditor*)(AssetEditor->FindEditorForAsset(LevelScriptBlueprint, true)))
				{
					BlueprintEditor->JumpToPin(FocusPin);
				}
			}
		}
	}
	else
	{
		if (UAssetEditorSubsystem* AssetEditor = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
		{
			AssetEditor->OpenEditorForAsset(Asset);
			if (IBlueprintEditor* BlueprintEditor = (IBlueprintEditor*)(AssetEditor->FindEditorForAsset(Asset, true)))
			{
				BlueprintEditor->JumpToPin(FocusPin);
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FStructsHelperEditorModule, StructsHelperEditor)
