#include "Cultivation/SoilTypes.h"

const TArray<FSoilDef>& GetAllSoils()
{
	// ItemId,            DisplayName,        Yield, Quality, Harvests, MinPhase
	static const TArray<FSoilDef> Defs = {
		{ TEXT("Soil_Basic"),   TEXT("Basic soil"),   1.00f, 1.00f, 3, 0 },
		{ TEXT("Soil_Rich"),    TEXT("Rich soil"),    1.25f, 1.15f, 4, 1 },
		{ TEXT("Soil_Premium"), TEXT("Premium soil"), 1.50f, 1.30f, 6, 2 },
	};
	return Defs;
}

bool GetSoilDef(FName ItemId, FSoilDef& Out)
{
	for (const FSoilDef& D : GetAllSoils())
	{
		if (D.ItemId == ItemId)
		{
			Out = D;
			return true;
		}
	}
	return false;
}

bool IsSoilItem(FName ItemId)
{
	return ItemId.ToString().StartsWith(TEXT("Soil_"));
}
