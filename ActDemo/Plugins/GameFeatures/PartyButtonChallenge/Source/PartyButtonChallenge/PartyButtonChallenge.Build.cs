using UnrealBuildTool;

public class PartyButtonChallenge : ModuleRules
{
    public PartyButtonChallenge(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PrivateDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "PartyFramework", "EnhancedInput", "InputCore" });
    }
}
