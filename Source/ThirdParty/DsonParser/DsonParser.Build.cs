using System.IO;
using UnrealBuildTool;

public class DsonParser : ModuleRules
{
    public DsonParser(ReadOnlyTargetRules Target) : base(Target)
    {
        Type = ModuleType.External;

        // Only supported on Win64 at this stage
        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            // Include path
            PublicIncludePaths.Add(Path.Combine(ModuleDirectory, "Include"));

            // Import library
            PublicAdditionalLibraries.Add(
                Path.Combine(ModuleDirectory, "Libs", "Win64", "DsonParser.lib"));

            // Delay-load the DLL so UBT stages it correctly
            PublicDelayLoadDLLs.Add("DsonParser.dll");

            // Stage the DLL for packaged builds
            RuntimeDependencies.Add(
                Path.Combine(ModuleDirectory, "Libs", "Win64", "DsonParser.dll"));
        }
    }
}