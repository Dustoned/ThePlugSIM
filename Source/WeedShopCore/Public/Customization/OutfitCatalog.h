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

	// Optionele accessoire-slots ("None" = niks dragen; Path == nullptr wordt overgeslagen).
	inline const FPart Headwear[] = {
		{ TEXT("None"),         nullptr },
		{ TEXT("Headphones"),   TEXT("/Game/Casual_Wear_Pack1/Mesh/Parts/Cloth/Accessories/Head_Accessories/SK_Headphones.SK_Headphones") },
		{ TEXT("Hairpin"),      TEXT("/Game/Casual_Wear_Pack1/Mesh/Parts/Cloth/Accessories/Head_Accessories/SK_Hairpin.SK_Hairpin") },
		{ TEXT("Cat ears"),     TEXT("/Game/Casual_Wear_Pack1/Mesh/Parts/Cloth/Accessories/Head_Accessories/SK_Cat_Ears.SK_Cat_Ears") },
		{ TEXT("Bunny ears"),   TEXT("/Game/Casual_Wear_Pack1/Mesh/Parts/Cloth/Accessories/Head_Accessories/SK_Bunny_Ears_1.SK_Bunny_Ears_1") },
		{ TEXT("Devil horns"),  TEXT("/Game/Casual_Wear_Pack1/Mesh/Parts/Cloth/Accessories/Head_Accessories/SK_Devil_Horns.SK_Devil_Horns") },
		{ TEXT("Horns"),        TEXT("/Game/Casual_Wear_Pack1/Mesh/Parts/Cloth/Accessories/Head_Accessories/SK_Horns_1.SK_Horns_1") },
		{ TEXT("Elf ears"),     TEXT("/Game/Casual_Wear_Pack1/Mesh/Parts/Cloth/Accessories/Head_Accessories/SK_Ears.SK_Ears") },
	};
	inline const FPart Necklaces[] = {
		{ TEXT("None"),           nullptr },
		{ TEXT("Chain"),          TEXT("/Game/Casual_Wear_Pack1/Mesh/Parts/Cloth/Accessories/Charms/SK_Chain_1.SK_Chain_1") },
		{ TEXT("Chain + cat"),    TEXT("/Game/Casual_Wear_Pack1/Mesh/Parts/Cloth/Accessories/Charms/SK_Chain_2_Cat.SK_Chain_2_Cat") },
		{ TEXT("Chain + moon"),   TEXT("/Game/Casual_Wear_Pack1/Mesh/Parts/Cloth/Accessories/Charms/SK_Chain_2_Moon.SK_Chain_2_Moon") },
		{ TEXT("Chain + triangle"), TEXT("/Game/Casual_Wear_Pack1/Mesh/Parts/Cloth/Accessories/Charms/SK_Chain_2_Tris.SK_Chain_2_Tris") },
		{ TEXT("Chain heavy"),    TEXT("/Game/Casual_Wear_Pack1/Mesh/Parts/Cloth/Accessories/Charms/SK_Chain_3.SK_Chain_3") },
	};
	inline const FPart SocksList[] = {
		{ TEXT("None"),        nullptr },
		{ TEXT("Socks"),       TEXT("/Game/Casual_Wear_Pack1/Mesh/Parts/Cloth/Accessories/Socks/SK_Socks_1.SK_Socks_1") },
		{ TEXT("Socks 2"),     TEXT("/Game/Casual_Wear_Pack1/Mesh/Parts/Cloth/Accessories/Socks/SK_Socks_2.SK_Socks_2") },
		{ TEXT("Long socks"),  TEXT("/Game/Casual_Wear_Pack1/Mesh/Parts/Cloth/Accessories/Socks/SK_Socks_Long_1.SK_Socks_Long_1") },
		{ TEXT("Long socks 2"),TEXT("/Game/Casual_Wear_Pack1/Mesh/Parts/Cloth/Accessories/Socks/SK_Socks_Long_2.SK_Socks_Long_2") },
	};

	// Altijd onder de kleding aanwezig (geen keuze-slot).
	inline const TCHAR* UnderwearPath = TEXT("/Game/Casual_Wear_Pack1/Mesh/Parts/Cloth/Accessories/Underwear/SK_Underwear.SK_Underwear");

	// Naakte basis-body (incl. hoofd) per meisje; index 0..2 = Girl 1..3.
	inline const TCHAR* FullBodyPaths[3] = {
		TEXT("/Game/Casual_Wear_Pack1/Mesh/Parts/Bodys/FullBody/SK_FullBody_Casual_1.SK_FullBody_Casual_1"),
		TEXT("/Game/Casual_Wear_Pack1/Mesh/Parts/Bodys/FullBody/SK_FullBody_Casual_2.SK_FullBody_Casual_2"),
		TEXT("/Game/Casual_Wear_Pack1/Mesh/Parts/Bodys/FullBody/SK_FullBody_Casual_3.SK_FullBody_Casual_3"),
	};

	inline int32 SlotCount() { return 7; }
	inline const TCHAR* SlotName(int32 Slot)
	{
		switch (Slot)
		{
		case 0: return TEXT("Top"); case 1: return TEXT("Pants"); case 2: return TEXT("Shoes"); case 3: return TEXT("Hair");
		case 4: return TEXT("Headwear"); case 5: return TEXT("Necklace"); case 6: return TEXT("Socks");
		}
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
		case 4: return UE_ARRAY_COUNT(Headwear);
		case 5: return UE_ARRAY_COUNT(Necklaces);
		case 6: return UE_ARRAY_COUNT(SocksList);
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
		case 4: return Headwear[I];
		case 5: return Necklaces[I];
		case 6: return SocksList[I];
		default: return Tops[I];
		}
	}
}
