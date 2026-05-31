#include "Placement/PlaceableTypes.h"

const TArray<FPlaceableDef>& GetAllPlaceables()
{
	// Cube/Cylinder basis-mesh = 100 cm; BoxHalf = 50 * schaal (en pot is een cilinder r=25, h=40).
	static const TArray<FPlaceableDef> Defs = {
		// ItemId,        DisplayName,   MeshPath,                         MeshScale,                  BoxHalf,                    bIsPot
		{ TEXT("Pot_Broken"),  TEXT("Broken pot"),  TEXT("/Engine/BasicShapes/Cylinder.Cylinder"), FVector(0.45f, 0.45f, 0.35f), FVector(23.f, 23.f, 18.f), true },
		{ TEXT("Pot_Clay"),    TEXT("Clay pot"),    TEXT("/Engine/BasicShapes/Cylinder.Cylinder"), FVector(0.50f, 0.50f, 0.40f), FVector(25.f, 25.f, 20.f), true },
		{ TEXT("Pot_Plastic"), TEXT("Plastic pot"), TEXT("/Engine/BasicShapes/Cylinder.Cylinder"), FVector(0.56f, 0.56f, 0.42f), FVector(28.f, 28.f, 21.f), true },
		{ TEXT("Pot_Fabric"),  TEXT("Fabric pot"),  TEXT("/Engine/BasicShapes/Cylinder.Cylinder"), FVector(0.72f, 0.72f, 0.46f), FVector(36.f, 36.f, 23.f), true },
		{ TEXT("Fridge"),   TEXT("Fridge"),   TEXT("/Engine/BasicShapes/Cube.Cube"),         FVector(0.6f, 0.6f, 1.8f),  FVector(30.f, 30.f, 90.f), false, 12000 },
		{ TEXT("Mattress"), TEXT("Mattress"), TEXT("/Engine/BasicShapes/Cube.Cube"),         FVector(2.0f, 1.0f, 0.5f),  FVector(100.f, 50.f, 25.f), false, 4000 },
		{ TEXT("Table"),    TEXT("Table"),    TEXT("/Engine/BasicShapes/Cube.Cube"),         FVector(1.2f, 0.8f, 0.8f),  FVector(60.f, 40.f, 40.f), false, 6000 },
		// ATM: spawnt een AAtm (interactief) en mag ook BUITEN geplaatst worden.
		{ TEXT("Atm"), TEXT("ATM"), TEXT("/Engine/BasicShapes/Cube.Cube"), FVector(0.5f, 0.35f, 1.4f), FVector(25.f, 18.f, 70.f), false, 0, /*bIsAtm*/ true, /*bAllowOutdoors*/ true },
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
