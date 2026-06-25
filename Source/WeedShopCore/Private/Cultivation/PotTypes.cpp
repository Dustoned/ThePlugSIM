#include "Cultivation/PotTypes.h"
#include "WeedShopCore.h"

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

// Gear-item-ids, op volgorde van upgrade-index (0..10, gelijk aan GetPotUpgrades()).
static const TCHAR* const GGearItems[] = {
	TEXT("Gear_Drainage"),  // 0 drainage
	TEXT("Gear_Insulation"),// 1 isolatie
	TEXT("Gear_Bloom"),     // 2 bloom booster
	TEXT("Gear_Lamp1"),     // 3 grow lamp I
	TEXT("Gear_Lamp2"),     // 4 grow lamp II
	TEXT("Gear_Lamp3"),     // 5 grow lamp III
	TEXT("Gear_Tent1"),     // 6 grow tent I
	TEXT("Gear_Tent2"),     // 7 grow tent II
	TEXT("Gear_Tent3"),     // 8 grow tent III
	TEXT("Gear_Water1"),    // 9 auto-water I
	TEXT("Gear_Water2"),    // 10 auto-water II
};

int32 GearUpgradeIndex(FName ItemId)
{
	for (int32 i = 0; i < UE_ARRAY_COUNT(GGearItems); ++i)
	{
		if (ItemId == FName(GGearItems[i])) { return i; }
	}
	return -1;
}

FName GearItemForUpgrade(int32 UpgIndex)
{
	if (UpgIndex < 0 || UpgIndex >= UE_ARRAY_COUNT(GGearItems)) { return NAME_None; }
	return FName(GGearItems[UpgIndex]);
}

const TArray<FPotUpgradeDef>& GetPotUpgrades()
{
	// Name, Desc, BaseCost, MinPotTier(0=Broken..3=Fabric), MinLevel, PrereqIndex
	static const TArray<FPotUpgradeDef> Defs = {
		/*0*/ { TEXT("Drainage layer"), TEXT("+10% max quality"),                     3000, 0, 1,  -1 },
		/*1*/ { TEXT("Insulation"),     TEXT("Dries out 2x slower"),                  4000, 0, 2,  -1 },
		/*2*/ { TEXT("Bloom booster"),  TEXT("+20% harvest yield"),                   5000, 0, 6,  -1 },
		// Grow lamp I/II/III - sneller groeien
		/*3*/ { TEXT("Grow lamp I"),    TEXT("+15% faster growth"),                   6000, 1, 5,  -1 },
		/*4*/ { TEXT("Grow lamp II"),   TEXT("+30% faster growth"),                  12000, 1, 16,  3 },
		/*5*/ { TEXT("Grow lamp III"),  TEXT("+50% faster growth"),                  22000, 2, 30,  4 },
		// Grow tent I/II/III - hoger kwaliteitsplafond
		/*6*/ { TEXT("Grow tent I"),    TEXT("+8% max quality"),                      7000, 1, 7,  -1 },
		/*7*/ { TEXT("Grow tent II"),   TEXT("+15% max quality"),                    13000, 1, 18,  6 },
		/*8*/ { TEXT("Grow tent III"),  TEXT("+22% max quality"),                    24000, 2, 32,  7 },
		// Auto-watering I/II - geen handmatig water meer
		/*9*/ { TEXT("Auto-water I"),   TEXT("Keeps itself watered"),                14000, 2, 13, -1 },
		/*10*/{ TEXT("Auto-water II"),  TEXT("Watered + nutrient dosing (+10% yield)"),26000, 3, 27,  9 },
	};
	return Defs;
}

int32 HighestOwnedTier(int32 Mask, const TArray<int32>& BitIndices)
{
	int32 Tier = 0;
	for (int32 t = 0; t < BitIndices.Num(); ++t)
	{
		if ((Mask & (1 << BitIndices[t])) != 0) { Tier = t + 1; }
	}
	return Tier;
}

TArray<int32> GearFamilyIndices(int32 UpgIndex)
{
	const TArray<FPotUpgradeDef>& Ups = GetPotUpgrades();
	TArray<int32> Out;
	if (!Ups.IsValidIndex(UpgIndex)) { return Out; }
	int32 Root = UpgIndex;
	while (Ups.IsValidIndex(Root) && Ups[Root].PrereqIndex >= 0) { Root = Ups[Root].PrereqIndex; }
	for (int32 i = 0; i < Ups.Num(); ++i)
	{
		int32 r = i;
		while (Ups.IsValidIndex(r) && Ups[r].PrereqIndex >= 0) { r = Ups[r].PrereqIndex; }
		if (r == Root) { Out.Add(i); }
	}
	Out.Sort();
	return Out;
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
	const int64 V = WeedRoundEuros((int64)Ups[UpgIndex].BaseCostCents * (Tier + 1));
	return Ups[UpgIndex].BaseCostCents > 0 ? (int32)FMath::Max<int64>(100, V) : (int32)V;
}
