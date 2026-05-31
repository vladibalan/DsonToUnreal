using System.IO;
using UnrealBuildTool;

public class DsonParser : ModuleRules
{
    public DsonParser(ReadOnlyTargetRules Target) : base(Target)
    {
        Type = ModuleType.External;

        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            // Include path only — no .lib, no delay-load
            PublicIncludePaths.Add(Path.Combine(ModuleDirectory, "Include"));

            // Stage the DLL for packaged builds
            RuntimeDependencies.Add(
                Path.Combine(ModuleDirectory, "Libs", "Win64", "DsonParser.dll"));
        }
    }
}