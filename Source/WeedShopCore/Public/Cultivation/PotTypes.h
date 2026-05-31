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

// Of dit item-id een pot is (begint met "Pot").
bool IsPotItem(FName ItemId);
