// ALampLinkMarker - in de LINK-MODUS een blauwe glow-bol OVER de lightbox van een gelinkte lamp (de lamp
// lijkt zo blauw). Niet-gelinkt = verborgen (maar nog steeds aanklikbaar). Interact toggelt de link op de
// schakelaar + de blauwe glow. Puur lokaal (geen replicatie), zoals APackLightSwitch.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interaction/Interactable.h"
#include "LampLinkMarker.generated.h"

class UStaticMeshComponent;
class UMaterialInstanceDynamic;
class APackLightSwitch;
class UPhoneClientComponent;

UCLASS()
class WEEDSHOPCORE_API ALampLinkMarker : public AActor, public IInteractable
{
	GENERATED_BODY()

public:
	ALampLinkMarker();

	// Sw = de schakelaar waarvoor we linken; InPhone = voor de inactiviteits-timer; InLampKey = MakeLampKey.
	void Init(APackLightSwitch* Sw, UPhoneClientComponent* InPhone, const FString& InLampKey);
	void RefreshLink(); // toon de blauwe glow alleen als de lamp aan deze schakelaar gelinkt is

	virtual void Interact_Implementation(APawn* InstigatorPawn) override;
	virtual FText GetInteractionPrompt_Implementation() const override;
	virtual bool IsClientLocalInteract() const override { return true; } // lokaal/cosmetisch, geen server-RPC

protected:
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Mesh;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> MID;
	TWeakObjectPtr<APackLightSwitch> Switch;
	TWeakObjectPtr<UPhoneClientComponent> Phone;
	FString LampKey;
};
