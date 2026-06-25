// Pot-tiers: betere pot = betere waterretentie (hogere max-kwaliteit), meer yield en (later)
// meer plant-plekken. Elke pot is een placeable die een AGrowPlant spawnt met deze stats.

#pragma once

#include "CoreMinimal.h"

struct FPotDef
{
	FName ItemId = NAME_None;
	FString DisplayName;
	float CareCap = 0.7f;     // max haalbare verzorging (waterretentie) -> kwaliteitsplafond
	float YieldMult = 1.0f;   // opbrengst-bonus
	int32 PlantSlots = 1;     // aantal planten in 1 pot (multi-slot komt in batch C)
	int32 BuyPriceCents = 1500;
	int32 SellPriceCents = 600;
	uint8 MinPhase = 0;       // EShopPhase: 0=Straatdealer,1=Winkel,2=Franchise
	FVector MeshScale = FVector(0.5f, 0.5f, 0.4f); // visuele grootte (verschilt per tier)
};

const TArray<FPotDef>& GetAllPots();

// Definitie voor een pot-item-id. False als het geen pot is.
bool GetPotDef(FName ItemId, FPotDef& Out);

// Per-pot upgrades (gekocht per geplaatste pot; verdwijnen als je de pot oppakt/verkoopt).
// Lamp/tent/auto-water hebben meerdere TIERS die met progressie (level + pot) vrijkomen; elke
// tier is een eigen bit in het masker, met een prereq naar de vorige tier.
struct FPotUpgradeDef
{
	FString DisplayName;
	FString Desc;
	int32 BaseCostCents = 3000;
	int32 MinPotTierIndex = 0;   // pas beschikbaar vanaf deze pot-tier (0=Broken ... 3=Fabric)
	int32 MinPlayerLevel = 1;    // pas beschikbaar vanaf dit speler-level
	int32 PrereqIndex = -1;      // eerst deze upgrade nodig (vorige tier), -1 = geen
};

// De beschikbare pot-upgrades (index = bit in het pot-upgrade-masker):
// 0 drainage, 1 isolatie, 2 bloom, 3/4/5 grow-lamp I/II/III, 6/7/8 grow-tent I/II/III, 9/10 auto-water I/II.
const TArray<FPotUpgradeDef>& GetPotUpgrades();

// Kosten van upgrade UpgIndex voor een pot van deze tier (schaalt met de tier).
int32 GetPotUpgradeCost(int32 UpgIndex, FName PotTier);

// Tier-index (0=Broken ... 3=Fabric) van een pot-id; -1 als onbekend.
int32 GetPotTierIndex(FName PotTier);

// Mag upgrade UpgIndex op een pot van deze tier (sommige, zoals auto-water, pas op latere potten)?
bool IsPotUpgradeAllowed(int32 UpgIndex, FName PotTier);

// Hoogste bezeten tier-bonus uit een masker, voor een reeks bit-indices (bv. lamp = {3,4,5}).
// Geeft het 1-gebaseerde tier-nummer terug (0 = geen).
int32 HighestOwnedTier(int32 Mask, const TArray<int32>& BitIndices);

// Alle tier-broers in dezelfde familie (Lamp/Tent/Water); {Ui} voor standalones.
TArray<int32> GearFamilyIndices(int32 UpgIndex);

// Of dit item-id een pot is (begint met "Pot").
bool IsPotItem(FName ItemId);

// --- Pot-gear: fysieke accessoires die je naast de pot zet (i.p.v. abstracte upgrades) ---
// Upgrade-index (0..10, zie GetPotUpgrades) van een gear-item-id; -1 als het geen gear is.
int32 GearUpgradeIndex(FName ItemId);
// Het gear-item-id voor een upgrade-index (NAME_None als ongeldig).
FName GearItemForUpgrade(int32 UpgIndex);
