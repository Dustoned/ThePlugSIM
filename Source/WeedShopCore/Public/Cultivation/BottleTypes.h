// Waterfles-tiers. Elke fles is een los inventory-item dat een eigen slot inneemt; betere flessen
// houden meer water vast (meer "slokken" voordat je moet bijvullen bij de gootsteen).

#pragma once

#include "CoreMinimal.h"

struct FBottleDef
{
	FName ItemId = NAME_None;
	FString DisplayName;
	int32 Charges = 3;          // aantal keer water geven voordat 'ie leeg is
	float WaterPerClick = 0.25f;// hoeveel WaterLevel (0..1) een plant erbij krijgt per keer water geven
	int32 BuyPriceCents = 1000;
	uint8 MinPhase = 0;         // EShopPhase: 0=Straatdealer,1=Winkel,2=Franchise
};

const TArray<FBottleDef>& GetAllBottles();

bool GetBottleDef(FName ItemId, FBottleDef& Out);

bool IsBottleItem(FName ItemId);
