using UnrealBuildTool;

public class ActDemoEditor : ModuleRules
{
    public ActDemoEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PrivateDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "ActDemo", "UnrealEd", "EditorFramework", "AssetRegistry", "PartyFramework", "Projects", "InputCore", "Slate", "SlateCore" });
    }
}
