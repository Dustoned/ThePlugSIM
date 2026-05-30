// AUpgradeStation — interactable koop-punt. Aankijken + E koopt de ingestelde upgrade met de
// gedeelde kas (server-authoritative via de interactie-component). Placeholder-mesh; later een
// echt meubel/UI met meerdere upgrades.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interaction/Interactable.h"
#include "UpgradeStation.generated.h"

class UStaticMeshComponent;

UCLASS()
class WEEDSHOPCORE_API AUpgradeStation : public AActor, public IInteractable
{
	GENERATED_BODY()

public:
	AUpgradeStation();

	// Welke upgrade dit station verkoopt (rij-naam in DT_Upgrades).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WeedShop|Upgrade")
	FName UpgradeId = NAME_None;

	virtual void Interact_Implementation(APawn* InstigatorPawn) override;
	virtual FText GetInteractionPrompt_Implementation() const override;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "WeedShop|Upgrade")
	TObjectPtr<UStaticMeshComponent> Mesh;
};
