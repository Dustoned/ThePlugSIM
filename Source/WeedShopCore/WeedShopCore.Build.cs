// WeedShopCore — eigen gameplay-module (gescheiden van de template-boilerplate in ThePlugSIM).
// Hier komt alle game-logica: economy, inventory, kweek, klanten, deal-systeem, milestones.

using UnrealBuildTool;

public class WeedShopCore : ModuleRules
{
	public WeedShopCore(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"UMG",
			"Slate",
			"SlateCore"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"AIModule",
			"NavigationSystem",
			"ImageWrapper",  // PNG-swatch -> wit masker (menu-knoppen kleurbaar maken)
			"MoviePlayer",   // loading screen tijdens level-reload (New Game/Load) i.p.v. zwart beeld
			"AssetRegistry", // wardrobe: pack-mappen scannen voor auto-detectie van alle kleding/haar
			"RHI",           // PipelineStateCache: PSO-precaching-status -> laadscherm wacht erop
			"RenderCore",    // FlushRenderingCommands: kaart-capture sync (nacht-lampen-cull rond CaptureScene)
			"ApplicationCore" // FDisplayMetrics: monitor onder het venster vinden (multi-monitor fullscreen)
		});
	}
}
