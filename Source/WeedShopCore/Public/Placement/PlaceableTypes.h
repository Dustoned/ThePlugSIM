// Registry van plaatsbare objecten (placeables). Eén bron van waarheid voor zowel het plaatsen
// (ghost + spawn) als het oppakken (welke item-id hoort bij dit object) en de footprint-check.
//
// "Pot" is speciaal: die spawnt een AGrowPlant (met kweek-flow). De rest spawnt een
// generieke APlaceableProp die z'n mesh/schaal uit deze tabel haalt.

#pragma once

#include "CoreMinimal.h"

struct FPlaceableDef
{
	FName ItemId = NAME_None;
	FString DisplayName;
	const TCHAR* MeshPath = nullptr;   // bv. /Engine/BasicShapes/Cube.Cube
	FVector MeshScale = FVector(1.f);  // component-schaal
	FVector BoxHalf = FVector(25.f, 25.f, 20.f); // halve afmetingen (cm) voor mesh-offset + footprint
	bool bIsPot = false;               // true -> spawnt AGrowPlant i.p.v. APlaceableProp
};

// Alle gedefinieerde placeables.
const TArray<FPlaceableDef>& GetAllPlaceables();

// Zoek de definitie voor een item-id. Geeft false als het item niet plaatsbaar is.
bool GetPlaceableDef(FName ItemId, FPlaceableDef& Out);
