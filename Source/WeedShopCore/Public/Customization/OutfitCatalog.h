// OutfitCatalog - gecureerde kleding-catalogus uit de Casual Wear Pack 1 voor speler-customization
// via de Wardrobe (kledingkast). Slots: 0=Top, 1=Pants, 2=Shoes, 3=Hair. Parts zijn skeletal meshes
// op het (compatible) UE4-mannequin-skelet en volgen de body via leader-pose.

#pragma once

#include "CoreMinimal.h"

namespace WeedOutfit
{
	struct FPart
	{
		const TCHAR* Name; // weergavenaam in de wardrobe-UI
		const TCHAR* Path; // /Game-pad van de skeletal mesh
	};

	inline const FPart Tops[] = {
		{ TEXT("Top 1"),       TEXT("/Game/Casual_Wear_Pack1/Mesh/Parts/Cloth/Torso/SK_Top_1.SK_Top_1") },
		{ TEXT("Top 2"),       TEXT("/Game/Casual_Wear_Pack1/Mesh/Parts/Cloth/Torso/SK_Top_2.SK_Top_2") },
		{ TEXT("Mini hoodie"), TEXT("/Game/Casual_Wear_Pack1/Mesh/Parts/Cloth/Torso/SK_Hoodies_Mini.SK_Hoodies_Mini") },
	};
	inline const FPart Legs[] = {
		{ TEXT("Baggy jeans"),        TEXT("/Game/Casual_Wear_Pack1/Mesh/Parts/Cloth/Legs/SK_Baggy_Jeans.SK_Baggy_Jeans") },
		{ TEXT("Baggy jeans + belt"), TEXT("/Game/Casual_Wear_Pack1/Mesh/Parts/Cloth/Legs/SK_Baggy_Jeans_Belt_1.SK_Baggy_Jeans_Belt_1") },
		{ TEXT("Shorts"),             TEXT("/Game/Casual_Wear_Pack1/Mesh/Parts/Cloth/Legs/SK_Shorts_1.SK_Shorts_1") },
		{ TEXT("Shorts + belt"),      TEXT("/Game/Casual_Wear_Pack1/Mesh/Parts/Cloth/Legs/SK_Shorts_1_Belt_1.SK_Shorts_1_Belt_1") },
		{ TEXT("Wide jeans"),         TEXT("/Game/Casual_Wear_Pack1/Mesh/Parts/Cloth/Legs/SK_Wide_Leg_Jeans.SK_Wide_Leg_Jeans") },
		{ TEXT("Wide jeans + belt"),  TEXT("/Game/Casual_Wear_Pack1/Mesh/Parts/Cloth/Legs/SK_Wide_Leg_Jeans_Belt_1.SK_Wide_Leg_Jeans_Belt_1") },
	};
	inline const FPart Shoes[] = {
		{ TEXT("Sneakers A"), TEXT("/Game/Casual_Wear_Pack1/Mesh/Parts/Cloth/Shoes/SK_Sneakers_2.SK_Sneakers_2") },
		{ TEXT("Sneakers B"), TEXT("/Game/Casual_Wear_Pack1/Mesh/Parts/Cloth/Shoes/SK_Sneakers_4.SK_Sneakers_4") },
		{ TEXT("Sneakers C"), TEXT("/Game/Casual_Wear_Pack1/Mesh/Parts/Cloth/Shoes/SK_Sneakers_5.SK_Sneakers_5") },
	};
	inline const FPart Hairs[] = {
		{ TEXT("Short"),           TEXT("/Game/Casual_Wear_Pack1/Mesh/Parts/Hairs/SK_HairShort.SK_HairShort") },
		{ TEXT("Short + cap"),     TEXT("/Game/Casual_Wear_Pack1/Mesh/Parts/Hairs/SK_Hairshort_Cap.SK_Hairshort_Cap") },
		{ TEXT("Short + hat"),     TEXT("/Game/Casual_Wear_Pack1/Mesh/Parts/Hairs/SK_Hairshort_Hat.SK_Hairshort_Hat") },
		{ TEXT("Braid"),           TEXT("/Game/Casual_Wear_Pack1/Mesh/Parts/Hairs/SK_Hair_Braid.SK_Hair_Braid") },
		{ TEXT("Medium"),          TEXT("/Game/Casual_Wear_Pack1/Mesh/Parts/Hairs/SK_Hair_Medium_1.SK_Hair_Medium_1") },
		{ TEXT("Medium + cap"),    TEXT("/Game/Casual_Wear_Pack1/Mesh/Parts/Hairs/SK_Hair_Medium_1_Cap.SK_Hair_Medium_1_Cap") },
		{ TEXT("Medium + panama"), TEXT("/Game/Casual_Wear_Pack1/Mesh/Parts/Hairs/SK_Hair_Medium_1_Panama.SK_Hair_Medium_1_Panama") },
	};

	// Altijd onder de kleding aanwezig (geen keuze-slot).
	inline const TCHAR* UnderwearPath = TEXT("/Game/Casual_Wear_Pack1/Mesh/Parts/Cloth/Accessories/Underwear/SK_Underwear.SK_Underwear");

	// Naakte basis-body (incl. hoofd) per meisje; index 0..2 = Girl 1..3.
	inline const TCHAR* FullBodyPaths[3] = {
		TEXT("/Game/Casual_Wear_Pack1/Mesh/Parts/Bodys/FullBody/SK_FullBody_Casual_1.SK_FullBody_Casual_1"),
		TEXT("/Game/Casual_Wear_Pack1/Mesh/Parts/Bodys/FullBody/SK_FullBody_Casual_2.SK_FullBody_Casual_2"),
		TEXT("/Game/Casual_Wear_Pack1/Mesh/Parts/Bodys/FullBody/SK_FullBody_Casual_3.SK_FullBody_Casual_3"),
	};

	inline int32 SlotCount() { return 4; }
	inline const TCHAR* SlotName(int32 Slot)
	{
		switch (Slot) { case 0: return TEXT("Top"); case 1: return TEXT("Pants"); case 2: return TEXT("Shoes"); case 3: return TEXT("Hair"); }
		return TEXT("?");
	}
	inline int32 PartCount(int32 Slot)
	{
		switch (Slot)
		{
		case 0: return UE_ARRAY_COUNT(Tops);
		case 1: return UE_ARRAY_COUNT(Legs);
		case 2: return UE_ARRAY_COUNT(Shoes);
		case 3: return UE_ARRAY_COUNT(Hairs);
		}
		return 0;
	}
	inline const FPart& PartAt(int32 Slot, int32 Index)
	{
		const int32 I = FMath::Clamp(Index, 0, PartCount(Slot) - 1);
		switch (Slot)
		{
		case 1: return Legs[I];
		case 2: return Shoes[I];
		case 3: return Hairs[I];
		default: return Tops[I];
		}
	}
}
