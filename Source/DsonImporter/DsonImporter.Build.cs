using UnrealBuildTool;

public class DsonImporter : ModuleRules
{
    public DsonImporter(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",  // UDsonAssetRecipe (Public header) derives from UObject
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "Engine",
            "UnrealEd",
            // Required for Phase 3 - skeletal mesh building
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
            "ImageWrapper",
        });
    }
}
