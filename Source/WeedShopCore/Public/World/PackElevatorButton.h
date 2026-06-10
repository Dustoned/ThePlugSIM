// APackElevatorButton - call-knop naast de liftdeur (per verdieping). F = lift naar deze verdieping
// roepen. Gebruikt de SM_ElevatorCallButton-mesh uit de pack.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interaction/Interactable.h"
#include "PackElevatorButton.generated.h"

class APackElevator;
class UStaticMeshComponent;

UCLASS()
class WEEDSHOPCORE_API APackElevatorButton : public AActor, public IInteractable
{
	GENERATED_BODY()

public:
	APackElevatorButton();

	void Setup(APackElevator* InElevator, int32 InFloorIdx);

	// Bordje boven de deuropening dat live toont op welke verdieping de cabine is (digit-mesh uit de pack).
	void SetupSign(const FVector& SignWorldLoc, const FRotator& SignRot);
	void SetDigit(int32 Digit);

	virtual void Interact_Implementation(APawn* InstigatorPawn) override;
	virtual FText GetInteractionPrompt_Implementation() const override;

protected:
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Mesh;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> DigitMesh;
	int32 CurDigit = -1;
	TWeakObjectPtr<APackElevator> Elevator;
	int32 FloorIdx = 0;
};
