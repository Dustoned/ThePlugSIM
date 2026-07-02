#include "Cultivation/BottleTypes.h"

const TArray<FBottleDef>& GetAllBottles()
{
	// ItemId,                  DisplayName,    Charges, WaterPerClick, Buy,   MinPhase
	static const TArray<FBottleDef> Defs = {
		{ TEXT("WaterBottle_Plastic"),  TEXT("Plastic bottle"), 3,  0.25f, 1000,  0 },
		{ TEXT("WaterBottle_Steel"),    TEXT("Steel bottle"),   6,  0.35f, 4500,  0 },
		{ TEXT("WaterBottle_Jerrycan"), TEXT("Jerry can"),      12, 0.50f, 15000, 1 },
		{ TEXT("WaterBottle_Tank"),     TEXT("Water tank"),     25, 0.70f, 45000, 2 },
	};
	return Defs;
}

bool GetBottleDef(FName ItemId, FBottleDef& Out)
{
	for (const FBottleDef& D : GetAllBottles())
	{
		if (D.ItemId == ItemId)
		{
			Out = D;
			return true;
		}
	}
	return false;
}

bool IsBottleItem(FName ItemId)
{
	return ItemId.ToString().StartsWith(TEXT("WaterBottle"));
}
