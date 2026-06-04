// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class ThePlugSIM : ModuleRules
{
	public ThePlugSIM(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"AIModule",
			"NavigationSystem",
			"StateTreeModule",
			"GameplayStateTreeModule",
			"UMG",
			"Slate"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { "WeedShopCore" });

		PublicIncludePaths.AddRange(new string[] {
			"ThePlugSIM",
			"ThePlugSIM/Variant_Horror",
			"ThePlugSIM/Variant_Horror/UI",
			"ThePlugSIM/Variant_Shooter",
			"ThePlugSIM/Variant_Shooter/AI",
			"ThePlugSIM/Variant_Shooter/UI",
			"ThePlugSIM/Variant_Shooter/Weapons"
		});

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
