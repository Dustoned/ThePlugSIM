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
}
