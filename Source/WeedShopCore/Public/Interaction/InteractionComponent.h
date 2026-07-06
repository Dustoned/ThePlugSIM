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

	// Minimale tijd (sec) tussen twee interacties bij snel opnieuw drukken — heel kort, dempt
	// alleen toevallige dubbele triggers. Ingedrukt houden spamt sowieso niet (zie edge-latch).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WeedShop|Interaction")
	float InteractCooldown = 0.05f;

	// UI bindt hierop om de interact-prompt te tonen/verbergen (lokale client).
	UPROPERTY(BlueprintAssignable, Category = "WeedShop|Interaction")
	FOnFocusedInteractableChanged OnFocusedInteractableChanged;

	// Roep dit aan vanuit je Enhanced Input-actie (E). Voert de interactie server-authoritative uit.
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Interaction")
	void TryInteract();

	// Het object dat de speler nu aankijkt (nullptr als geen). Alleen zinvol op de lokale client.
	UFUNCTION(BlueprintPure, Category = "WeedShop|Interaction")
	AActor* GetFocusedActor() const { return FocusedActor.Get(); }

	// ND7.12: center-screen interact-prompt (de popup onder het crosshair) aan/uit; standaard AAN.
	// Persistentie via GConfig in GameUserSettings.ini (zelfde patroon als UHotkeyHintWidget::AreHintsEnabled).
	// UIT -> UHotkeyHintWidget verbergt de popup en toont de prompt-tekst in de controls-kaart rechtsonder.
	static bool IsInteractPromptEnabled();
	static void SetInteractPromptEnabled(bool bEnabled);

	// CO-OP: relay de gedeelde lamp-staat naar de server. APackLightSwitch (bReplicates=false) roept dit aan op de
	// LOKALE pawn's component; de RPC schrijft dan WorldSync (server-authoritative). Publieke wrapper om de RPC.
	void RelayLampState(uint32 LampId, bool bOn, float Brightness01) { ServerSetLamp(LampId, bOn, Brightness01); }

protected:
	virtual void BeginPlay() override;

	// Tijdstip (wereld-seconden) van de laatste interactie — voor de korte anti-dubbel cooldown.
	double LastInteractTime = -1000.0;

	// Edge-latch: TryInteract wordt door Enhanced Input "Triggered" elke frame aangeroepen
	// zolang de toets ingedrukt is. We voeren maar één keer per indruk uit. De latch blijft
	// staan zolang de toets gehouden wordt en wordt pas in de tick gereset als er een frame
	// voorbijgaat zónder TryInteract-aanroep (= toets losgelaten).
	bool bInteractLatched = false;

	// Werd TryInteract deze frame aangeroepen? In de tick gebruikt om de latch te resetten.
	bool bInteractRequestedThisFrame = false;

	// Het laatst gefocuste interact-bare object (lokaal bepaald).
	UPROPERTY(Transient)
	TWeakObjectPtr<AActor> FocusedActor;

	// Server-RPC: voert de interactie authoritative uit na validatie.
	UFUNCTION(Server, Reliable)
	void ServerInteract(AActor* Target);

	// CO-OP gedeelde deur: toggle de deur (stabiel positie-id) in de WorldSync-component op de GameState.
	UFUNCTION(Server, Reliable)
	void ServerToggleDoor(uint32 DoorId);

	// CO-OP gedeelde lift: schrijf de doel-verdieping van een lift (stabiel positie-id) in WorldSync op de
	// GameState. Zelfde patroon als ServerToggleDoor, maar met een verdieping i.p.v. een open/dicht-toggle.
	UFUNCTION(Server, Reliable)
	void ServerCallElevator(uint32 ElevId, int32 Floor);

	// CO-OP: betaal de achterstallige huur van de starter-deur (stabiel positie-id, zoals ServerToggleDoor).
	// Server betaalt met de cash van DEZE speler + wist de gedeelde overdue-vlag (deur is bReplicates=false).
	UFUNCTION(Server, Reliable)
	void ServerPayRent(uint32 DoorId);

	// CO-OP: schrijf de gedeelde lamp-staat (aan/uit + helderheid) via WorldSync. De schakelaar is
	// bReplicates=false, dus de client relayet z'n toggle/dim via dit RPC naar de server.
	UFUNCTION(Server, Reliable)
	void ServerSetLamp(uint32 LampId, bool bOn, float Brightness01);

	// De daadwerkelijke uitvoering (draait op de server, of in SP/host direct).
	void PerformInteract(AActor* Target);

	// Of deze pawn dicht genoeg bij Target staat om te mogen interacten (server-side check).
	bool IsWithinReach(const AActor* Target) const;

	// Herberekent het aangekeken object en vuurt de delegate als het veranderde (lokaal).
	void UpdateFocus();

	// Camerastandpunt van de bezittende pawn (valt terug op actor-eyes als er geen controller is).
	bool GetViewPoint(FVector& OutLocation, FRotator& OutRotation) const;
};
