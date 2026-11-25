using UnrealBuildTool;

public class PartyCrystalHunt : ModuleRules
{
    public PartyCrystalHunt(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PrivateDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "PartyFramework" });
    }
}
