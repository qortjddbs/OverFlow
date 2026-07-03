// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class OverflowJOLJAK : ModuleRules
{
	public OverflowJOLJAK(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"AIModule",
			"StateTreeModule",
			"GameplayStateTreeModule",
			"UMG",
			"Slate"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { });

		PublicIncludePaths.AddRange(new string[] {
			"OverflowJOLJAK",
			"OverflowJOLJAK/Variant_Platforming",
			"OverflowJOLJAK/Variant_Platforming/Animation",
			"OverflowJOLJAK/Variant_Combat",
			"OverflowJOLJAK/Variant_Combat/AI",
			"OverflowJOLJAK/Variant_Combat/Animation",
			"OverflowJOLJAK/Variant_Combat/Gameplay",
			"OverflowJOLJAK/Variant_Combat/Interfaces",
			"OverflowJOLJAK/Variant_Combat/UI",
			"OverflowJOLJAK/Variant_SideScrolling",
			"OverflowJOLJAK/Variant_SideScrolling/AI",
			"OverflowJOLJAK/Variant_SideScrolling/Gameplay",
			"OverflowJOLJAK/Variant_SideScrolling/Interfaces",
			"OverflowJOLJAK/Variant_SideScrolling/UI"
		});

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
