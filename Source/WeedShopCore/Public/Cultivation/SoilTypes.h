// Soil-tiers voor de kweekpot. Soil moet in de pot vóór je kunt planten; betere soil (verderop
// in de progressie) geeft meer yield + kwaliteit en gaat meer oogsten mee.

#pragma once

#include "CoreMinimal.h"

struct FSoilDef
{
	FName ItemId = NAME_None;
	FString DisplayName;
	float YieldMult = 1.f;      // vermenigvuldigt de opbrengst
	float QualityMult = 1.f;    // vermenigvuldigt de THC/kwaliteit
	int32 Harvests = 3;         // aantal oogsten voor de soil op is
	uint8 MinPhase = 0;         // EShopPhase als uint8: 0=Straatdealer,1=Winkel,2=Franchise
};

const TArray<FSoilDef>& GetAllSoils();

// Definitie voor een soil-item-id. False als het geen soil is.
bool GetSoilDef(FName ItemId, FSoilDef& Out);

// Of dit item-id een soil is (begint met "Soil_").
bool IsSoilItem(FName ItemId);
