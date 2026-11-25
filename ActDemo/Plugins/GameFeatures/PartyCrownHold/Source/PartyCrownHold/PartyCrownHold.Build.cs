using UnrealBuildTool;

public class PartyCrownHold : ModuleRules
{
    public PartyCrownHold(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PrivateDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "PartyFramework" });
    }
}
