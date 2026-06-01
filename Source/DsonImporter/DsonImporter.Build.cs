using UnrealBuildTool;

public class DsonImporter : ModuleRules
{
    public DsonImporter(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "CoreUObject",
            "Engine",
            "UnrealEd",
            // Required for Phase 3 — skeletal mesh building
            "AnimationCore",
            "MeshDescription",
            "StaticMeshDescription",
            "SkeletalMeshDescription",
            "MeshBuilder",
            "MeshUtilities",
            "RenderCore",
            "RHI",
            "AssetTools",
            "Projects",
            "DsonParser",
            "Slate",
            "SlateCore",
            "EditorStyle",
            "ToolMenus",
            "InputCore",
            "DesktopPlatform",
            "AssetRegistry",
            "ContentBrowser",
            "EditorFramework",
        });
    }
}