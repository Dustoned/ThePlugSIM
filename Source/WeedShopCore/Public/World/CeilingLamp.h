// ACeilingLamp — klein plafond-/spotlamp-model met een warm licht. Plaatsbaar (koop bij de
// furniture store) en oppakbaar (G). Interactable: E = licht aan/uit (ook nodig om gefocust en
// dus oppakbaar te zijn).

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interaction/Interactable.h"
#include "CeilingLamp.generated.h"

class UStaticMeshComponent;
class UPointLightComponent;

UCLASS()
class WEEDSHOPCORE_API ACeilingLamp : public AActor, public IInteractable
{
	GENERATED_BODY()

public:
	ACeilingLamp();

	virtual void Interact_Implementation(APawn* InstigatorPawn) override;
	virtual FText GetInteractionPrompt_Implementation() const override;

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> Root;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Mount; // ophangplaat tegen het plafond
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Rod;   // steeltje tussen plaat en kapje
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Shade; // kapje (kegel)
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Bulb;  // lampje
	UPROPERTY(VisibleAnywhere) TObjectPtr<UPointLightComponent> Light; // warm omni-licht
};
