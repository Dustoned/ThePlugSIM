// OutfitCatalog - kleding-catalogus uit de Casual Wear Pack 1 voor speler-customization via de Wardrobe.
// AUTO-DETECTIE: de catalogus wordt bij eerste gebruik gevuld door de pack-mappen te scannen
// (AssetRegistry). Zo verschijnt ELKE kledingmesh/haar/accessoire automatisch als optie, zonder dat we
// elk pad met de hand hoeven te listen. Slots: 0=Top, 1=Pants, 2=Shoes, 3=Hair, 4=Headwear, 5=Necklace,
// 6=Socks. Parts zijn skeletal meshes op het (compatible) UE4-mannequin-skelet en volgen de body via
// leader-pose. Body-gecombineerde "Optimized" meshes + body-specifieke varianten worden gefilterd.

#pragma once

#include "CoreMinimal.h"

namespace WeedOutfit
{
	struct FPart
	{
		const TCHAR* Name; // weergavenaam in de wardrobe-UI
		const TCHAR* Path; // /Game-pad van de skeletal mesh (nullptr = "None", niks dragen)
	};

	// Altijd onder de kleding aanwezig (geen keuze-slot).
	inline const TCHAR* UnderwearPath = TEXT("/Game/Casual_Wear_Pack1/Mesh/Parts/Cloth/Accessories/Underwear/SK_Underwear.SK_Underwear");

	// Naakte basis-body (incl. hoofd) per meisje; index 0..2 = Girl 1..3.
	inline const TCHAR* FullBodyPaths[3] = {
		TEXT("/Game/Casual_Wear_Pack1/Mesh/Parts/Bodys/FullBody/SK_FullBody_Casual_1.SK_FullBody_Casual_1"),
		TEXT("/Game/Casual_Wear_Pack1/Mesh/Parts/Bodys/FullBody/SK_FullBody_Casual_2.SK_FullBody_Casual_2"),
		TEXT("/Game/Casual_Wear_Pack1/Mesh/Parts/Bodys/FullBody/SK_FullBody_Casual_3.SK_FullBody_Casual_3"),
	};

	// Male body (citizens Tony, basis-body waar de kleren op layeren).
	inline const TCHAR* MaleBodyPath = TEXT("/Game/Citizens_Pack/Meshes/Citizens_Pack_Parts_Tony/SK_Citizens_Pack_Tony_Body.SK_Citizens_Pack_Tony_Body");

	// Gevuld via een lazy AssetRegistry-scan bij het eerste gebruik (zie OutfitCatalog.cpp). bMale = de
	// male-catalogus (gecureerde citizens-kleren) i.p.v. de female-catalogus (Casual-pack, auto-scan).
	WEEDSHOPCORE_API int32 SlotCount();
	WEEDSHOPCORE_API const TCHAR* SlotName(int32 Slot);
	WEEDSHOPCORE_API int32 PartCount(int32 Slot, bool bMale = false);
	WEEDSHOPCORE_API const FPart& PartAt(int32 Slot, int32 Index, bool bMale = false);
}
