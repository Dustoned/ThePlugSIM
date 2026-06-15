// APackLightSwitch - lichtschakelaar aan de muur in je appartement. De schakelaar neemt de
// plafondlampen in zijn buurt over van de dag/nacht-klok (zie ADayNightController): die volgen dan
// niet meer de straatlantaarns maar jouw aan/uit + dimmer-stand, per kamer onthouden over sessies.
//
//  - TAP (kort drukken op de interact-toets) = licht aan/uit.
//  - HOLD (interact-toets ~0.4s vasthouden) = dimmer-popup met verticale slider (helderheid kiezen).
//
// De schakelaar claimt elke plafondlamp binnen ControlRadius waarvoor HIJ de dichtstbijzijnde
// schakelaar is -> twee schakelaars in hetzelfde appartement (grote kamer + badkamer) verdelen de
// lampen vanzelf. Tap/hold worden lokaal afgehandeld door het keystate van de speler te pollen, zodat
// er geen wijziging in de character/interaction-input nodig is. Licht is lokaal/cosmetisch.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interaction/Interactable.h"
#include "PackLightSwitch.generated.h"

class UStaticMeshComponent;
class UPointLightComponent;
class UMaterialInstanceDynamic;
class APlayerController;

UCLASS()
class WEEDSHOPCORE_API APackLightSwitch : public AActor, public IInteractable
{
	GENERATED_BODY()

public:
	APackLightSwitch();

	virtual void Tick(float DeltaSeconds) override;

	// InKey = unieke persist-sleutel (bv. "apt12_main"); InRadius = claim-straal (cm) rond de schakelaar.
	void Setup(const FString& InKey, float InRadius);

	// Licht aan/uit (TAP). Forceeraan = bv. bij openen dimmer.
	void ToggleOnOff();
	void SetOn(bool bNewOn);
	bool IsOn() const { return bOn; }

	// Helderheid 0..1 (dimmer). Default = 0.5 (midden).
	void SetBrightness01(float V);
	float GetBrightness01() const { return Brightness01; }

	virtual void Interact_Implementation(APawn* InstigatorPawn) override;
	virtual FText GetInteractionPrompt_Implementation() const override;

protected:
	virtual void BeginPlay() override;

	// Lampen claimen die binnen ControlRadius vallen en waarvoor wij de dichtstbijzijnde schakelaar zijn.
	void ClaimLamps();
	// Huidige aan/uit + dim toepassen op alle geclaimde lampen + emissive-boxen.
	void ApplyToLamps();

	void Load();
	void Save() const;

	UPROPERTY() TObjectPtr<UStaticMeshComponent> Plate;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> PlateMID;

	// Geclaimde lampen (zwak: de mesh/level kan ge-unload worden) + emissive-boxen met hun basis-brightness.
	TArray<TWeakObjectPtr<UPointLightComponent>> Lights;
	TArray<TWeakObjectPtr<UMaterialInstanceDynamic>> Emis;
	TArray<float> EmisBase;

	FString PersistKey;
	float ControlRadius = 520.f;
	bool bOn = true;
	float Brightness01 = 0.5f; // dimmer-default = midden

	float ClaimTimer = 0.f; // periodiek opnieuw claimen (lampen kunnen later in-streamen)

	// Tap/hold: Interact() (op indrukken) 'armt'; de Tick lost op met het echte keystate van de speler.
	bool bPressArmed = false;
	bool bHoldFired = false;
	float HoldTimer = 0.f;
	TWeakObjectPtr<APlayerController> PressPC;
	TWeakObjectPtr<APawn> PressPawn;
	static constexpr float HoldRequired = 0.4f;
};
