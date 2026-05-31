#include "Placement/PlaceableTypes.h"

const TArray<FPlaceableDef>& GetAllPlaceables()
{
	// Cube/Cylinder basis-mesh = 100 cm; BoxHalf = 50 * schaal (en pot is een cilinder r=25, h=40).
	static const TArray<FPlaceableDef> Defs = {
		// ItemId,        DisplayName,   MeshPath,                         MeshScale,                  BoxHalf,                    bIsPot
		{ TEXT("Pot"),      TEXT("Grow pot"), TEXT("/Engine/BasicShapes/Cylinder.Cylinder"), FVector(0.5f, 0.5f, 0.4f),  FVector(25.f, 25.f, 20.f), true  },
		{ TEXT("Fridge"),   TEXT("Fridge"),   TEXT("/Engine/BasicShapes/Cube.Cube"),         FVector(0.8f, 1.5f, 1.0f),  FVector(40.f, 75.f, 50.f), false },
		{ TEXT("Mattress"), TEXT("Mattress"), TEXT("/Engine/BasicShapes/Cube.Cube"),         FVector(2.0f, 1.0f, 0.5f),  FVector(100.f, 50.f, 25.f), false },
		{ TEXT("Table"),    TEXT("Table"),    TEXT("/Engine/BasicShapes/Cube.Cube"),         FVector(1.2f, 0.8f, 0.8f),  FVector(60.f, 40.f, 40.f), false },
	};
	return Defs;
}

bool GetPlaceableDef(FName ItemId, FPlaceableDef& Out)
{
	for (const FPlaceableDef& D : GetAllPlaceables())
	{
		if (D.ItemId == ItemId)
		{
			Out = D;
			return true;
		}
	}
	return false;
}
