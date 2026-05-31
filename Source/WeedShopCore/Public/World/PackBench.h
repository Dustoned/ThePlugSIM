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

	virtual void Interact_Implementation(APawn* InstigatorPawn) override;
	virtual FText GetInteractionPrompt_Implementation() const override;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "WeedShop|Pack")
	TObjectPtr<UStaticMeshComponent> Mesh;
};
