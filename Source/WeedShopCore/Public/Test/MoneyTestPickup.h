// AMoneyTestPickup — wegwerp test-object om de keten interactie -> server-RPC -> replicated kas
// te verifiëren. Aankijken + interacteren (E) voegt server-authoritative geld toe aan de
// gedeelde kas op de GameState. Zichtbaar als kubus (engine-mesh), verwijder later.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interaction/Interactable.h"
#include "MoneyTestPickup.generated.h"

class UStaticMeshComponent;

UCLASS()
class WEEDSHOPCORE_API AMoneyTestPickup : public AActor, public IInteractable
{
	GENERATED_BODY()

public:
	AMoneyTestPickup();

	// Bedrag dat één interactie oplevert (cents). €10,00 = 1000.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WeedShop|Test")
	int32 AmountCents = 1000;

	// IInteractable
	virtual void Interact_Implementation(APawn* InstigatorPawn) override;
	virtual FText GetInteractionPrompt_Implementation() const override;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "WeedShop|Test")
	TObjectPtr<UStaticMeshComponent> Mesh;
};
