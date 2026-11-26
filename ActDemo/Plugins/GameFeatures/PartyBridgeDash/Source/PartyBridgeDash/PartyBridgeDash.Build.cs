using UnrealBuildTool;

public class PartyBridgeDash : ModuleRules
{
    public PartyBridgeDash(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PrivateDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "PartyFramework" });
    }
}
