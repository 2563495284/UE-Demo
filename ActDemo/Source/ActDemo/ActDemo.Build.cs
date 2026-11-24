// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class ActDemo : ModuleRules
{
	public ActDemo(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
	
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput", "OnlineSubsystem" });

		PrivateDependencyModuleNames.AddRange(new string[] { "GeometryCore", "ProceduralMeshComponent", "GameFeatures", "PartyFramework", "Projects", "OnlineSubsystemUtils", "Slate", "SlateCore", "UMG", "MoviePlayer", "Sockets", "Networking", "RenderCore", "RHI" });
		PublicIncludePaths.Add(ModuleDirectory);

	}
}
