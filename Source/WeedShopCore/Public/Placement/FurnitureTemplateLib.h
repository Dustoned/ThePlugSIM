// FurnitureTemplates — authoring-helper om een meubel-layout PER WONING-TYPE op te slaan en als
// default toe te passen. Workflow: start een sandbox (free-build), zet meubels neer in een
// representatieve kamer per type, en sla op. Verse games spawnen dan die layout in elke woning van
// dat type (incl. NPC-woningen). Opslag = simpel tekstbestand in ProjectSaved/.

#pragma once

#include "CoreMinimal.h"

class UWorld;
class ACityGenerator;

struct FFurnitureEntry
{
	FName ItemId = NAME_None;
	FVector Local = FVector::ZeroVector; // offset t.o.v. de kamer (home InteriorPos), wereld-assen
	float Yaw = 0.f;
};

namespace FurnitureTemplates
{
	// Pad naar het template-bestand.
	WEEDSHOPCORE_API FString FilePath();

	// Type-sleutel voor een woning: soort (Apt/Row) + kamergrootte (op 50cm gequantiseerd), zodat
	// een grote kamer / grote rij z'n eigen sjabloon krijgt. Bijv. "Apt_44x18", "Row_60x20".
	WEEDSHOPCORE_API FString TypeKey(bool bApartment, const FVector& RoomHalf);

	// Tel hoeveel woningen er per type zijn (voor "hoeveel types moet ik doen"). Sleutel -> aantal.
	WEEDSHOPCORE_API void CountHomeTypes(ACityGenerator* City, TMap<FString, int32>& Out);

	// Capture de huidige geplaatste meubels per woning-type -> schrijf naar het bestand. Per type wordt
	// de woning met de MEESTE meubels als sjabloon genomen. Geeft het aantal opgeslagen types terug.
	WEEDSHOPCORE_API int32 SaveFromWorld(UWorld* W, ACityGenerator* City);

	// Lees de templates (type -> entries). False als er geen bestand is.
	WEEDSHOPCORE_API bool LoadTemplates(TMap<FString, TArray<FFurnitureEntry>>& Out);

	// Spawn één entry (server) in een woning. RoomHalf wordt gebruikt om binnen de kamer te clampen.
	WEEDSHOPCORE_API AActor* SpawnEntry(UWorld* W, const FFurnitureEntry& E, const FVector& HomeInterior, const FVector& RoomHalf);

	// Verwijder alle geplaatste meubel-actors (om opnieuw in te richten). Geeft aantal verwijderd terug.
	WEEDSHOPCORE_API int32 ClearPlaced(UWorld* W);
}
