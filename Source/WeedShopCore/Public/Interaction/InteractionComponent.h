// UInteractionComponent — zet deze op je speler-pawn (BP-character). Doet elke tick een
// line-trace vanuit het camerastandpunt, vindt het object dat hij aankijkt als dat IInteractable
// implementeert, en houdt de UI op de hoogte zodat je een prompt kunt tonen.
//
// Editor-koppeling:
//  1. Voeg deze component toe aan je BP_Character (Add Component -> "Interaction").
//  2. Bind een Enhanced Input-actie (bv. IA_Interact op E) die TryInteract() aanroept.
//  3. Optioneel: bind OnFocusedInteractableChanged in je HUD-widget om de prompt te tonen/verbergen.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "InteractionComponent.generated.h"

// NewFocus is nullptr als er niets (meer) wordt aangekeken. Prompt is leeg in dat geval.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnFocusedInteractableChanged, AActor*, NewFocus, const FText&, Prompt);

UCLASS(ClassGroup = (WeedShop), meta = (BlueprintSpawnableComponent))
class WEEDSHOPCORE_API UInteractionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UInteractionComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// Reikwijdte van de interactie-trace (cm).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WeedShop|Interaction")
	float InteractionDistance = 250.f;

	// Collision-channel waarop getraced wordt.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WeedShop|Interaction")
	TEnumAsByte<ECollisionChannel> TraceChannel = ECC_Visibility;

	// UI bindt hierop om de interact-prompt te tonen/verbergen.
	UPROPERTY(BlueprintAssignable, Category = "WeedShop|Interaction")
	FOnFocusedInteractableChanged OnFocusedInteractableChanged;

	// Roep dit aan vanuit je Enhanced Input-actie (E). Voert Interact uit op het aangekeken object.
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Interaction")
	void TryInteract();

	// Het object dat de speler nu aankijkt (nullptr als geen).
	UFUNCTION(BlueprintPure, Category = "WeedShop|Interaction")
	AActor* GetFocusedActor() const { return FocusedActor.Get(); }

protected:
	virtual void BeginPlay() override;

	// Het laatst gefocuste interact-bare object.
	UPROPERTY(Transient)
	TWeakObjectPtr<AActor> FocusedActor;

	// Herberekent het aangekeken object en vuurt de delegate als het veranderde.
	void UpdateFocus();

	// Camerastandpunt van de bezittende pawn (valt terug op actor-eyes als er geen controller is).
	bool GetViewPoint(FVector& OutLocation, FRotator& OutRotation) const;
};
