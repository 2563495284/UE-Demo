using UnrealBuildTool;

public class PartyFramework : ModuleRules
{
    public PartyFramework(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "GameFeatures", "NetCore", "InputCore" });
        PrivateDependencyModuleNames.AddRange(new[] { "GeometryCore", "ProceduralMeshComponent" });
    }
}
