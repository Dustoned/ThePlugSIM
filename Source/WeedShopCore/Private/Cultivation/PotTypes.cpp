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
