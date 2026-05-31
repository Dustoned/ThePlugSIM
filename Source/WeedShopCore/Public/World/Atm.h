// AAtm — geldautomaat in de wereld. Aankijken + interacten opent de Bank-app op je telefoon, waar je
// cash kunt storten (witwassen): belasting bij binnenkomst, daglimiet en heat-risico. Het openen van de
// UI gebeurt lokaal (de character vangt dit af); de storting zelf loopt server-authoritative.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interaction/Interactable.h"
#include "Atm.generated.h"

class UStaticMeshComponent;

UCLASS()
class WEEDSHOPCORE_API AAtm : public AActor, public IInteractable
{
	GENERATED_BODY()

public:
	AAtm();

	// IInteractable — de prompt; de echte actie (Bank-app openen) doet de character lokaal.
	virtual void Interact_Implementation(APawn* InstigatorPawn) override;
	virtual FText GetInteractionPrompt_Implementation() const override;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "WeedShop|ATM")
	TObjectPtr<UStaticMeshComponent> Mesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "WeedShop|ATM")
	TObjectPtr<UStaticMeshComponent> Screen;
};
