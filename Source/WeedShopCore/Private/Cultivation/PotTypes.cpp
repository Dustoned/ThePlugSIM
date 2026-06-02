#include "Cultivation/PotTypes.h"

const TArray<FPotDef>& GetAllPots()
{
	// ItemId,           DisplayName,      CareCap, Yield, Slots, Buy,    Sell,  MinPhase, MeshScale
	static const TArray<FPotDef> Defs = {
		{ TEXT("Pot_Broken"),  TEXT("Broken pot"),  0.55f, 0.90f, 1, 1500,  600,   0, FVector(0.45f, 0.45f, 0.40f) },
		{ TEXT("Pot_Clay"),    TEXT("Clay pot"),    0.70f, 1.00f, 1, 4000,  1800,  0, FVector(0.50f, 0.50f, 0.40f) },
		{ TEXT("Pot_Plastic"), TEXT("Plastic pot"), 0.85f, 1.10f, 2, 10000, 4500,  1, FVector(0.58f, 0.58f, 0.40f) },
		{ TEXT("Pot_Fabric"),  TEXT("Fabric pot"),  1.00f, 1.25f, 6, 35000, 16000, 2, FVector(0.75f, 0.75f, 0.42f) },
	};
	return Defs;
}

bool GetPotDef(FName ItemId, FPotDef& Out)
{
	for (const FPotDef& D : GetAllPots())
	{
		if (D.ItemId == ItemId)
		{
			Out = D;
			return true;
		}
	}
	return false;
}

bool IsPotItem(FName ItemId)
{
	return ItemId.ToString().StartsWith(TEXT("Pot"));
}

const TArray<FPotUpgradeDef>& GetPotUpgrades()
{
	// DisplayName, Desc, BaseCost, MinPotTierIndex (0=Broken,1=Clay,2=Plastic,3=Fabric)
	static const TArray<FPotUpgradeDef> Defs = {
		{ TEXT("Drainage layer"), TEXT("+10% max quality (better water retention)"),  3000, 0 },
		{ TEXT("Insulation"),     TEXT("Dries out slower (less watering needed)"),     4000, 0 },
		{ TEXT("Bloom booster"),  TEXT("+20% harvest yield"),                          5000, 0 },
		{ TEXT("Grow tent"),      TEXT("Sealed tent: +15% quality, holds care"),       7000, 1 },
		{ TEXT("LED grow lamp"),  TEXT("+30% faster growth"),                          9000, 1 },
		{ TEXT("Auto-watering"),  TEXT("Keeps itself watered - no manual watering"),  14000, 2 },
	};
	return Defs;
}

int32 GetPotTierIndex(FName PotTier)
{
	const TArray<FPotDef>& Pots = GetAllPots();
	for (int32 i = 0; i < Pots.Num(); ++i)
	{
		if (Pots[i].ItemId == PotTier) { return i; }
	}
	return -1;
}

bool IsPotUpgradeAllowed(int32 UpgIndex, FName PotTier)
{
	const TArray<FPotUpgradeDef>& Ups = GetPotUpgrades();
	if (!Ups.IsValidIndex(UpgIndex)) { return false; }
	const int32 Tier = GetPotTierIndex(PotTier);
	if (Tier < 0) { return false; }
	return Tier >= Ups[UpgIndex].MinPotTierIndex;
}

int32 GetPotUpgradeCost(int32 UpgIndex, FName PotTier)
{
	const TArray<FPotUpgradeDef>& Ups = GetPotUpgrades();
	if (!Ups.IsValidIndex(UpgIndex))
	{
		return 0;
	}
	// Schaal met de tier-index (Broken=0 ... Fabric=3): duurdere potten = duurdere upgrades.
	const int32 Tier = FMath::Max(0, GetPotTierIndex(PotTier));
	return Ups[UpgIndex].BaseCostCents * (Tier + 1);
}
