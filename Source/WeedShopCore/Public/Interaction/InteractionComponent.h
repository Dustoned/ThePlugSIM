// UInteractionComponent — zet deze op je speler-pawn (BP-character). Doet (alleen voor de
// lokaal bestuurde speler) elke tick een line-trace vanuit het camerastandpunt, vindt het
// object dat hij aankijkt als dat IInteractable implementeert, en houdt de UI op de hoogte
// zodat je een prompt kunt tonen.
//
// CO-OP: de feitelijke interactie is server-authoritative. De client bepaalt lokaal welk
// object hij aankijkt (camera-trace), maar de wereld-/state-mutatie draait op de server via
// een Server-RPC. Zo zien alle spelers hetzelfde resultaat. Dit is het standaardpatroon voor
// elk systeem in deze game (zie DECISIONS.md).
//
// Vereisten voor co-op: de pawn waarop dit zit moet repliceren en in bezit zijn van een
// PlayerController (anders wordt de Server-RPC niet gerouteerd).
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

	// Reikwijdte van de interactie-trace (cm). Ook de server-side validatie gebruikt deze (met marge).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WeedShop|Interaction")
	float InteractionDistance = 250.f;

	// Collision-channel waarop getraced wordt.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WeedShop|Interaction")
	TEnumAsByte<ECollisionChannel> TraceChannel = ECC_Visibility;

	// UI bindt hierop om de interact-prompt te tonen/verbergen (lokale client).
	UPROPERTY(BlueprintAssignable, Category = "WeedShop|Interaction")
	FOnFocusedInteractableChanged OnFocusedInteractableChanged;

	// Roep dit aan vanuit je Enhanced Input-actie (E). Voert de interactie server-authoritative uit.
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Interaction")
	void TryInteract();

	// Het object dat de speler nu aankijkt (nullptr als geen). Alleen zinvol op de lokale client.
	UFUNCTION(BlueprintPure, Category = "WeedShop|Interaction")
	AActor* GetFocusedActor() const { return FocusedActor.Get(); }

protected:
	virtual void BeginPlay() override;

	// Het laatst gefocuste interact-bare object (lokaal bepaald).
	UPROPERTY(Transient)
	TWeakObjectPtr<AActor> FocusedActor;

	// Server-RPC: voert de interactie authoritative uit na validatie.
	UFUNCTION(Server, Reliable)
	void ServerInteract(AActor* Target);

	// De daadwerkelijke uitvoering (draait op de server, of in SP/host direct).
	void PerformInteract(AActor* Target);

	// Of deze pawn dicht genoeg bij Target staat om te mogen interacten (server-side check).
	bool IsWithinReach(const AActor* Target) const;

	// Herberekent het aangekeken object en vuurt de delegate als het veranderde (lokaal).
	void UpdateFocus();

	// Camerastandpunt van de bezittende pawn (valt terug op actor-eyes als er geen controller is).
	bool GetViewPoint(FVector& OutLocation, FRotator& OutRotation) const;
};
