using UnrealBuildTool;

public class PartyReefRun : ModuleRules
{
    public PartyReefRun(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PrivateDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "PartyFramework" });
    }
}
