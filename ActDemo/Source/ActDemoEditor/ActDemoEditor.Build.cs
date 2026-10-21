using UnrealBuildTool;
public class ActDemoEditor : ModuleRules
{
 public ActDemoEditor(ReadOnlyTargetRules Target) : base(Target)
 {
  PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
  PrivateDependencyModuleNames.AddRange(new[] { "BSPUtils", "AnimGraph", "AnimGraphRuntime" });
  PrivateDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "ActDemo", "UnrealEd", "AudioEditor", "AssetRegistry", "Kismet", "KismetCompiler", "BlueprintGraph", "UMG", "UMGEditor", "Slate", "SlateCore", "EnhancedInput", "GameplayTags", "StateTreeModule", "GameplayStateTreeModule", "StateTreeEditorModule", "AIModule", "NavigationSystem", "Niagara", "RenderCore", "InputCore", "PropertyBindingUtils" });
 }
}
