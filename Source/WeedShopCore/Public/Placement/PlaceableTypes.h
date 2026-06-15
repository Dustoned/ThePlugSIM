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
	int32 SellCents = 0;               // verkoopwaarde bij de supplier (meubels; pots gaan via 70% koopprijs)
	bool bIsAtm = false;               // true -> spawnt AAtm (geldautomaat)
	bool bAllowOutdoors = false;       // true -> mag ook buiten geplaatst (negeert de "alleen binnen"-regel)
	bool bIsDryRack = false;           // true -> spawnt ADryingRack (RackTier = ItemId)
	bool bIsPackBench = false;         // true -> spawnt APackBench (verpak-tafel)
	bool bIsShelf = false;             // true -> spawnt AStorageShelf (voorraad-opslag in de shop)
	bool bIsSink = false;              // true -> spawnt AWaterSink (waterfles vullen)
	bool bIsLamp = false;              // true -> spawnt ACeilingLamp (warme spot-lamp)
	bool bIsWallMount = false;         // true -> hangt aan een VERTICALE muur (rug tegen de muur) i.p.v. op de vloer
	bool bIsBed = false;               // true -> slapen (nacht overslaan tot 07:00) + spawn-/laadpunt hier
	bool bIsProcessor = false;         // true -> spawnt AProcessorMachine (Mesh_/Press_: hasj-keten)
	bool bIsSafe = false;              // true -> spawnt AAtm in safe-modus (kluis: cash veilig stashen)
	bool bIsWardrobe = false;          // true -> kledingkast: interact opent het outfit-menu (speler-customization)
	bool bIsStructure = false;         // true -> bouw-onderdeel (muur/vloer/plafond): altijd grid-snap, vrij plaatsbaar (dev building-tool)
	bool bIsCeilingPiece = false;      // true -> structure-plafond: komt 320cm boven de verdieping-vloer
	bool bIsDoubleWall = false;        // true -> muur krijgt een gespiegelde tweede mesh (pack-muren zijn enkelzijdig)
	bool bIsHingedDoor = false;        // true -> draaiende deur (F = open/dicht, scharnier op de mesh-pivot)
	bool bIsLightSwitch = false;       // true -> spawnt APackLightSwitch (tap = aan/uit, hold = dimmer; claimt plafondlampen)
};

// Alle gedefinieerde placeables.
const TArray<FPlaceableDef>& GetAllPlaceables();

// Zoek de definitie voor een item-id. Geeft false als het item niet plaatsbaar is.
WEEDSHOPCORE_API bool GetPlaceableDef(FName ItemId, FPlaceableDef& Out);
