using UnrealBuildTool;

public class PartyFloodGates : ModuleRules
{
    public PartyFloodGates(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PrivateDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "PartyFramework" });
    }
}
