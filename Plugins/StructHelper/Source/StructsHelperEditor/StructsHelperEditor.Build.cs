// Copyright 2025 Just2Devs. All Rights Reserved.

using UnrealBuildTool;

public class StructsHelperEditor : ModuleRules
{
    public StructsHelperEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
            }
        );

        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                "CoreUObject",
                "Engine",
                "Slate",
                "SlateCore",
                "ToolMenus",
                "BlueprintGraph",
                "UnrealEd"
            }
        );
    }
}