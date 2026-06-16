// AActivitySpotManager - dev-tool-ondersteuning: 'activity spots'. Op een gemarkeerde plek verschijnt op een
// ingesteld tijdvak een NPC die daar een gekozen animatie doet (leunen, bellen, dansen, ...). De manager
// laadt de spots uit ProjectSaved/ActivitySpots.txt, en spawnt/despawn't per spot een inerte ACustomerBase
// zodra de klok in het tijdvak valt. Eén centrale, server-only manager (gespawned door de GameMode).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ActivitySpotManager.generated.h"

class ACustomerBase;

USTRUCT()
struct FActivitySpotData
{
	GENERATED_BODY()

	FString Name;
	FString Map;
	FVector Pos = FVector::ZeroVector;
	float Yaw = 0.f;
	float HourStart = 0.f;     // tijdvak [Start, End) op de spelklok (0..24). Start>End = over middernacht.
	float HourEnd = 24.f;
	int32 AnimIdx = 0;         // index in ACustomerBase::ActivityAnim*

	TWeakObjectPtr<ACustomerBase> Npc; // huidige bezetter (zolang het tijdvak actief is)
};

UCLASS()
class WEEDSHOPCORE_API AActivitySpotManager : public AActor
{
	GENERATED_BODY()

public:
	AActivitySpotManager();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	// Dev-tool: voeg live een spot toe (op de speler-plek) + schrijf 'm naar het bestand. Spawnt direct een
	// NPC als het tijdvak nu actief is (instant feedback). Geeft het totale aantal spots terug.
	int32 AddSpotLive(const FVector& Pos, float Yaw, int32 AnimIdx, float HourStart, float HourEnd);

	// Dev-tool: verwijder de dichtstbijzijnde spot bij een wereldlocatie (despawn z'n NPC + herschrijf bestand).
	// Geeft de naam van de verwijderde spot terug (leeg = niets gevonden binnen bereik).
	FString RemoveNearestSpot(const FVector& Near, float MaxDist = 400.f);

	// Pad naar het opslagbestand (ProjectSaved/ActivitySpots.txt).
	static FString GetSaveFile();

private:
	void LoadSpots();
	void RewriteFile() const;
	FString CurrentMap() const;
	bool IsWindowActive(const FActivitySpotData& S, float Hour) const;
	float CurrentHour() const;
	ACustomerBase* SpawnActivityNpc(const FActivitySpotData& S);

	UPROPERTY()
	TArray<FActivitySpotData> Spots;

	float EvalTimer = 0.f;
};
