// APackBench — verpak-tafel. Aankijken + interacten opent het verpak-menu (UPackWidget), waar je
// gedroogde wiet in bakjes/jars verdeelt tot verkoopbare voorraad. UI-openen gebeurt lokaal (character).

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interaction/Interactable.h"
#include "PackBench.generated.h"

class UStaticMeshComponent;

UCLASS()
class WEEDSHOPCORE_API APackBench : public AActor, public IInteractable
{
	GENERATED_BODY()

public:
	APackBench();

	// Tier-id (gezet bij plaatsen). Bepaalt hoeveel zakjes je per keer verpakt (hogere tier = sneller).
	UPROPERTY(BlueprintReadOnly, Category = "WeedShop|Pack")
	FName BenchTier = TEXT("Bench_Pack");

	// Aantal containers dat dit werkblad per keer verwerkt.
	int32 GetPackPerAction() const;
	static int32 PackPerActionFor(FName Tier);

	virtual void Interact_Implementation(APawn* InstigatorPawn) override;
	virtual FText GetInteractionPrompt_Implementation() const override;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "WeedShop|Pack")
	TObjectPtr<UStaticMeshComponent> Mesh;
};
