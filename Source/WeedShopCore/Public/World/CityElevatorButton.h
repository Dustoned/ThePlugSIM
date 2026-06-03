// ACityElevatorButton — een klein interactbaar knopje met een echt nummer (UTextRender). In de cabine
// = verdieping-keuzeknop; op een verdieping = oproepknop. F drukt 'm in -> stuurt/roept de lift.
// Lokaal/cosmetisch (net als de lift zelf).

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interaction/Interactable.h"
#include "CityElevatorButton.generated.h"

class UStaticMeshComponent;
class USceneComponent;
class UTextRenderComponent;
class UMaterialInstanceDynamic;
class ACityElevator;

UCLASS()
class WEEDSHOPCORE_API ACityElevatorButton : public AActor, public IInteractable
{
	GENERATED_BODY()

public:
	ACityElevatorButton();

	// Koppel aan een lift + verdieping. bInCall = oproepknop op een verdieping (anders cabine-keuzeknop).
	void Setup(ACityElevator* InElevator, int32 InFloor, bool bInCall);

	virtual void Interact_Implementation(APawn* InstigatorPawn) override;
	virtual FText GetInteractionPrompt_Implementation() const override;

protected:
	virtual void Tick(float DeltaSeconds) override;

	UPROPERTY() TObjectPtr<USceneComponent> Root;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Panel;
	UPROPERTY() TObjectPtr<UTextRenderComponent> Label;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> PanelMat;

	TWeakObjectPtr<ACityElevator> Elevator;
	int32 Floor = 0;
	bool bCall = false;
	float Flash = 0.f; // korte oplicht-feedback bij indrukken
};
