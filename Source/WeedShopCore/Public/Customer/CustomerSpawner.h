// ACustomerSpawner — spawnt klanten op één punt en stuurt ze naar een plek in de buurt (via de
// navmesh). Server-authoritative. Plaats er één in de map (bij de deur/ingang).

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CustomerSpawner.generated.h"

class ACustomerBase;

UCLASS()
class WEEDSHOPCORE_API ACustomerSpawner : public AActor
{
	GENERATED_BODY()

public:
	ACustomerSpawner();

	// Welke klant-klasse gespawnd wordt (leeg = ACustomerBase).
	UPROPERTY(EditAnywhere, Category = "Spawn")
	TSubclassOf<ACustomerBase> CustomerClass;

	UPROPERTY(EditAnywhere, Category = "Spawn")
	int32 MaxCustomers = 3;

	// TEST: spawn slechts ÉÉN klant, met hoge stat (koopklaar), netjes in het centrale park (PlayerStart).
	// Zo zweven er geen random NPC's meer in het midden; alleen deze ene om mee te testen.
	UPROPERTY(EditAnywhere, Category = "Spawn")
	bool bTestSingleHighStat = true;

	UPROPERTY(EditAnywhere, Category = "Spawn")
	float SpawnInterval = 10.f;

	// Hoe ver van het spawn-punt de klanten gaan staan.
	UPROPERTY(EditAnywhere, Category = "Spawn")
	float SpotRadius = 350.f;

protected:
	virtual void BeginPlay() override;
	void TrySpawn();
	// Maakt van C de koopklare test-klant op de park-plek (idempotent).
	void SetupTestCustomer(ACustomerBase* C, const FVector& Park);

	FTimerHandle SpawnTimer;

	UPROPERTY(Transient)
	TArray<TObjectPtr<ACustomerBase>> Spawned;
};
