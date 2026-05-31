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
            "MeshDescription",
            "SkeletalMeshDescription",
            "MeshUtilities",
            "RenderCore",
            "RHI",
            "AssetTools",
            "Projects",
            "DsonParser",
        });
    }
}