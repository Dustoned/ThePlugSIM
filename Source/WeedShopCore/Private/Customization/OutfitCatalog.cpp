// Auto-detectie van de wardrobe-catalogus: scan de Casual-Wear-pack-mappen via de AssetRegistry en bouw
// per slot een lijst van alle skeletal-mesh-kledingstukken. Lazy + gecached (één keer per proces).

#include "Customization/OutfitCatalog.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "AssetRegistry/ARFilter.h"
#include "Engine/SkeletalMesh.h"
#include "Modules/ModuleManager.h"

namespace WeedOutfit
{
	struct FSlotDef { const TCHAR* Name; const TCHAR* Folder; bool bHasNone; };

	// Welke pack-map bij welk slot hoort. bHasNone = een "None"-keuze (niks dragen) als eerste optie.
	static const FSlotDef GSlots[] = {
		{ TEXT("Top"),      TEXT("/Game/Casual_Wear_Pack1/Mesh/Parts/Cloth/Torso"),                          false },
		{ TEXT("Pants"),    TEXT("/Game/Casual_Wear_Pack1/Mesh/Parts/Cloth/Legs"),                           false },
		{ TEXT("Shoes"),    TEXT("/Game/Casual_Wear_Pack1/Mesh/Parts/Cloth/Shoes"),                          false },
		{ TEXT("Hair"),     TEXT("/Game/Casual_Wear_Pack1/Mesh/Parts/Hairs"),                                false },
		{ TEXT("Headwear"), TEXT("/Game/Casual_Wear_Pack1/Mesh/Parts/Cloth/Accessories/Head_Accessories"),   true },
		{ TEXT("Necklace"), TEXT("/Game/Casual_Wear_Pack1/Mesh/Parts/Cloth/Accessories/Charms"),             true },
		{ TEXT("Socks"),    TEXT("/Game/Casual_Wear_Pack1/Mesh/Parts/Cloth/Accessories/Socks"),              true },
	};
	static constexpr int32 GSlotCount = UE_ARRAY_COUNT(GSlots);

	struct FCatalog
	{
		TArray<FPart> Slots[GSlotCount];
		TArray<FString*> Storage; // heap-strings waar de FPart-pointers naar wijzen (stabiel, proces-leven)
		bool bBuilt = false;
	};
	static FCatalog& Cat() { static FCatalog C; return C; }

	// Bewaar een string stabiel en geef een const TCHAR* die het hele proces geldig blijft.
	static const TCHAR* Persist(const FString& S)
	{
		FString* P = new FString(S);
		Cat().Storage.Add(P);
		return **P;
	}

	// "SK_Hair_Medium_1_Cap" -> "Hair Medium 1 Cap".
	static FString Pretty(const FString& AssetName)
	{
		FString N = AssetName;
		N.RemoveFromStart(TEXT("SK_"));
		N.ReplaceInline(TEXT("_"), TEXT(" "));
		return N.TrimStartAndEnd();
	}

	// Body-gecombineerde "Optimized" meshes (geen losse laag -> botsen met de body) en body-specifieke
	// "_Casual_#"-varianten overslaan: die horen niet universeel als kledingstuk in de kast.
	static bool Skip(const FString& AssetName)
	{
		return AssetName.Contains(TEXT("Optimized")) || AssetName.Contains(TEXT("_Casual_"));
	}

	static void Build()
	{
		FCatalog& C = Cat();
		if (C.bBuilt) { return; }
		C.bBuilt = true;

		IAssetRegistry& AR = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
		for (int32 s = 0; s < GSlotCount; ++s)
		{
			if (GSlots[s].bHasNone) { C.Slots[s].Add({ TEXT("None"), nullptr }); }

			const FString Folder = GSlots[s].Folder;
			AR.ScanPathsSynchronous({ Folder }, /*bForceRescan*/ false); // zorg dat de map gescand is

			FARFilter Filter;
			Filter.PackagePaths.Add(FName(*Folder));
			Filter.bRecursivePaths = true;
			Filter.ClassPaths.Add(USkeletalMesh::StaticClass()->GetClassPathName());

			TArray<FAssetData> Assets;
			AR.GetAssets(Filter, Assets);
			// Stabiele, alfabetische volgorde -> save-indices blijven consistent tussen sessies.
			Assets.Sort([](const FAssetData& A, const FAssetData& B) { return A.AssetName.LexicalLess(B.AssetName); });

			for (const FAssetData& A : Assets)
			{
				const FString AssetName = A.AssetName.ToString();
				if (Skip(AssetName)) { continue; }
				C.Slots[s].Add({ Persist(Pretty(AssetName)), Persist(A.GetObjectPathString()) });
			}

			// Nooit een leeg slot teruggeven (voorkomt clamp-problemen): val terug op "None".
			if (C.Slots[s].Num() == 0) { C.Slots[s].Add({ TEXT("None"), nullptr }); }
		}
	}

	// === MALE catalogus: de complete (assembled) citizens-Tony-looks. Elk is een volledig aangekleed
	// personage (body + normale kleren + accessoire ingebakken) -> rendert betrouwbaar, geen losse-delen-
	// gedoe. De "Top"-slot kiest de look; de body-mesh volgt die keuze (zie ApplySkinMesh case 5).
#define CITM(p) TEXT("/Game/Citizens_Pack/Meshes/" p "." p)
	static const FPart MaleLooks[] = {
		{ TEXT("Look 1"), CITM("SK_Citizens_Pack_Tony_A") },
		{ TEXT("Look 2"), CITM("SK_Citizens_Pack_Tony_B") },
		{ TEXT("Look 3"), CITM("SK_Citizens_Pack_Tony_C") },
		{ TEXT("Look 4"), CITM("SK_Citizens_Pack_Tony_D") },
	};
	static const FPart MaleNone[] = { { TEXT("None"), nullptr } };
#undef CITM

	// === CITIZEN_MAN (skin 6): modulaire male-parts (zelfde 7-slot-structuur als de vrouw, maar male-body).
	//     Parts liggen plat in /Game/Citizen_man_01/mesh/ ; head zit op slot 6 (body is headless).
#define CMAN(p) TEXT("/Game/Citizen_man_01/mesh/citizen_man_01_" p ".citizen_man_01_" p)
	static const FPart CitMan_Top[] = {
		{ TEXT("None"), nullptr },
		{ TEXT("Shirt 1"), CMAN("shirt_01") }, { TEXT("Shirt 2"), CMAN("shirt_02") },
		{ TEXT("T-Shirt 1"), CMAN("t_shirt_01") }, { TEXT("T-Shirt 2"), CMAN("t_shirt_02") },
		{ TEXT("Sleeveless 1"), CMAN("sleevless_shirt_01") }, { TEXT("Sleeveless 2"), CMAN("sleevless_shirt_02") },
		{ TEXT("Jacket 1"), CMAN("jacket_classic_01") }, { TEXT("Jacket 2"), CMAN("jacket_classic_02") },
		{ TEXT("Leather 1"), CMAN("jacket_leather_01") }, { TEXT("Leather 2"), CMAN("jacket_leather_02") },
		{ TEXT("Leather 3"), CMAN("jacket_leather_03") },
	};
	static const FPart CitMan_Pants[] = {
		{ TEXT("Classic"), CMAN("pants_classic_01") }, { TEXT("Jeans"), CMAN("pants_jeans_01") }, { TEXT("Shorts"), CMAN("shorts_01") },
	};
	static const FPart CitMan_Shoes[] = {
		{ TEXT("Classic"), CMAN("shoes_classic_01") }, { TEXT("Slippers"), CMAN("shoes_slippers_01") }, { TEXT("Sneakers"), CMAN("shoes_sneakers_01") },
	};
	static const FPart CitMan_Hair[]  = { { TEXT("None"), nullptr }, { TEXT("Hair"), CMAN("hair_01") } };
	static const FPart CitMan_Hat[]   = { { TEXT("None"), nullptr }, { TEXT("Hat"),  CMAN("hat_01") } };
	static const FPart CitMan_Glass[] = { { TEXT("None"), nullptr }, { TEXT("Glasses"), CMAN("glasses_frame_01") } };
	static const FPart CitMan_Head[]  = {
		{ TEXT("Head 1"), CMAN("head_01") }, { TEXT("Head 2"), CMAN("head_02") }, { TEXT("Head 3"), CMAN("head_03") },
		{ TEXT("Head 4"), CMAN("head_04") }, { TEXT("Head 5"), CMAN("head_05") },
	};
#undef CMAN

	// Per-slot index -> tabel. Slot 6 (was "Socks") = Head ; slot 5 (was "Necklace") = Glasses ; slot 4 = Hat.
	static const FPart* const CitManSlots[] = {
		CitMan_Top, CitMan_Pants, CitMan_Shoes, CitMan_Hair, CitMan_Hat, CitMan_Glass, CitMan_Head,
	};
	static const int32 CitManCounts[] = {
		UE_ARRAY_COUNT(CitMan_Top), UE_ARRAY_COUNT(CitMan_Pants), UE_ARRAY_COUNT(CitMan_Shoes),
		UE_ARRAY_COUNT(CitMan_Hair), UE_ARRAY_COUNT(CitMan_Hat), UE_ARRAY_COUNT(CitMan_Glass), UE_ARRAY_COUNT(CitMan_Head),
	};
	static int32 CitManCount(int32 Slot)
	{
		return (Slot >= 0 && Slot < UE_ARRAY_COUNT(CitManCounts)) ? CitManCounts[Slot] : 1;
	}
	static const FPart& CitManAt(int32 Slot, int32 Index)
	{
		static const FPart None{ TEXT("None"), nullptr };
		if (Slot < 0 || Slot >= UE_ARRAY_COUNT(CitManSlots)) { return None; }
		return CitManSlots[Slot][FMath::Clamp(Index, 0, CitManCounts[Slot] - 1)];
	}

	static int32 MaleCount(int32 Slot)
	{
		// Slot 0 (Top) = de look-keuze; alle andere slots zitten al in de assembled look (None).
		return (Slot == 0) ? UE_ARRAY_COUNT(MaleLooks) : UE_ARRAY_COUNT(MaleNone);
	}
	static const FPart& MaleAt(int32 Slot, int32 Index)
	{
		if (Slot != 0) { return MaleNone[0]; }
		return MaleLooks[FMath::Clamp(Index, 0, (int32)UE_ARRAY_COUNT(MaleLooks) - 1)];
	}

	int32 SlotCount() { return GSlotCount; }

	const TCHAR* SlotName(int32 Slot)
	{
		return (Slot >= 0 && Slot < GSlotCount) ? GSlots[Slot].Name : TEXT("?");
	}

	int32 PartCount(int32 Slot, bool bMale)
	{
		if (Slot < 0 || Slot >= GSlotCount) { return 0; }
		if (bMale) { return MaleCount(Slot); }
		Build();
		return Cat().Slots[Slot].Num();
	}

	const FPart& PartAt(int32 Slot, int32 Index, bool bMale)
	{
		static const FPart None{ TEXT("None"), nullptr };
		if (Slot < 0 || Slot >= GSlotCount) { return None; }
		if (bMale) { return MaleAt(Slot, Index); }
		Build();
		if (Cat().Slots[Slot].Num() == 0) { return None; }
		const int32 I = FMath::Clamp(Index, 0, Cat().Slots[Slot].Num() - 1);
		return Cat().Slots[Slot][I];
	}

	int32 PartCountM(int32 Slot, EMaleKind Kind)
	{
		if (Slot < 0 || Slot >= GSlotCount) { return 0; }
		return (Kind == EMaleKind::CitizenMan) ? CitManCount(Slot) : MaleCount(Slot);
	}
	const FPart& PartAtM(int32 Slot, int32 Index, EMaleKind Kind)
	{
		static const FPart None{ TEXT("None"), nullptr };
		if (Slot < 0 || Slot >= GSlotCount) { return None; }
		return (Kind == EMaleKind::CitizenMan) ? CitManAt(Slot, Index) : MaleAt(Slot, Index);
	}
	const TCHAR* SlotNameM(int32 Slot, EMaleKind Kind)
	{
		if (Kind != EMaleKind::CitizenMan) { return (Slot == 0) ? TEXT("Look") : SlotName(Slot); }
		static const TCHAR* Labels[] = { TEXT("Top"), TEXT("Pants"), TEXT("Shoes"), TEXT("Hair"), TEXT("Hat"), TEXT("Glasses"), TEXT("Face") };
		return (Slot >= 0 && Slot < UE_ARRAY_COUNT(Labels)) ? Labels[Slot] : TEXT("?");
	}

	// === GIRL VARIANTEN (complete meshes): Gamer (skin 3) + School (skin 4). De wardrobe verbergt voor deze
	//     skins de Casual-kledingslots en toont een variant-switcher; ApplySkinMesh laadt de gekozen mesh.
	struct FGirlVar { const TCHAR* Name; const TCHAR* Path; };
	static const FGirlVar GGamerVars[] = {
		{ TEXT("Outfit 1"), TEXT("/Game/Gamer_Girl/Mesh/SK_GamerGirl_01.SK_GamerGirl_01") },
		{ TEXT("Outfit 2"), TEXT("/Game/Gamer_Girl/Mesh/SK_GamerGirl_02.SK_GamerGirl_02") },
		{ TEXT("Outfit 3"), TEXT("/Game/Gamer_Girl/Mesh/SK_GamerGirl_03.SK_GamerGirl_03") },
	};
	static const FGirlVar GSchoolVars[] = {
		{ TEXT("Casual"),   TEXT("/Game/SchoolGirl/Mesh/SK_SchoolGirl_CasualOutfit.SK_SchoolGirl_CasualOutfit") },
		{ TEXT("School"),   TEXT("/Game/SchoolGirl/Mesh/SK_SchoolGirl_SchoolOutfit.SK_SchoolGirl_SchoolOutfit") },
		{ TEXT("Dress"),    TEXT("/Game/SchoolGirl/Mesh/SK_SchoolGirl_DressOutfit.SK_SchoolGirl_DressOutfit") },
		{ TEXT("Sport"),    TEXT("/Game/SchoolGirl/Mesh/SK_SchoolGirl_SportOutfit.SK_SchoolGirl_SportOutfit") },
		{ TEXT("Swimsuit"), TEXT("/Game/SchoolGirl/Mesh/SK_SchoolGirl_SwimSuit.SK_SchoolGirl_SwimSuit") },
	};
	static const FGirlVar& GirlVar(uint8 Skin, int32 Idx)
	{
		static const FGirlVar None{ TEXT("?"), nullptr };
		if (Skin == 3) return GGamerVars[FMath::Clamp(Idx, 0, (int32)UE_ARRAY_COUNT(GGamerVars) - 1)];
		if (Skin == 4) return GSchoolVars[FMath::Clamp(Idx, 0, (int32)UE_ARRAY_COUNT(GSchoolVars) - 1)];
		return None;
	}
	int32 GirlVariantCount(uint8 Skin)
	{
		if (Skin == 3) return UE_ARRAY_COUNT(GGamerVars);
		if (Skin == 4) return UE_ARRAY_COUNT(GSchoolVars);
		return 0;
	}
	const TCHAR* GirlVariantPath(uint8 Skin, int32 Idx) { return GirlVar(Skin, Idx).Path; }
	const TCHAR* GirlVariantName(uint8 Skin, int32 Idx) { return GirlVar(Skin, Idx).Name; }
}
