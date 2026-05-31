// AWaterSink — gootsteen in huis. Aankijken + interact (E) vult je waterfles tot het maximum.
// Server-authoritative.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interaction/Interactable.h"
#include "WaterSink.generated.h"

class UStaticMeshComponent;

UCLASS()
class WEEDSHOPCORE_API AWaterSink : public AActor, public IInteractable
{
	GENERATED_BODY()

public:
	AWaterSink();

	virtual void Interact_Implementation(APawn* InstigatorPawn) override;
	virtual FText GetInteractionPrompt_Implementation() const override;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "WeedShop|Sink")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "WeedShop|Sink")
	TObjectPtr<UStaticMeshComponent> Mesh;
};
