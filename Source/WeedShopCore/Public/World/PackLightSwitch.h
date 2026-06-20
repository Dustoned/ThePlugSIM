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

	// --- Handmatige lamp-links (klikbare markers in de link-modus) ---
	static FString MakeLampKey(const FVector& WorldPos); // stabiele 50cm-grid-sleutel per lamp
	bool HasManualLinks() const { return LinkedLampKeys.Num() > 0; }
	bool IsLampLinked(const FString& LampKey) const { return LinkedLampKeys.Contains(LampKey); }
	void ToggleLampLink(const FString& LampKey); // toggle + opslaan + meteen opnieuw claimen
	void SaveLinks() const;
	void LoadLinks();
	const FString& GetPersistKey() const { return PersistKey; }
	float GetControlRadius() const { return ControlRadius; }
	// Link-modus visual: alle lampen in de buurt even overnemen + AANzetten, gelinkte BLAUW kleuren. Uit = terug.
	void SetLinkPreview(bool bEnable);

	virtual void Interact_Implementation(APawn* InstigatorPawn) override;
	virtual FText GetInteractionPrompt_Implementation() const override;
	virtual bool IsClientLocalInteract() const override { return true; } // lampen lokaal/cosmetisch per speler

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override; // bij oppakken: lampen terug naar de klok

	// Lampen claimen die binnen ControlRadius vallen en waarvoor wij de dichtstbijzijnde schakelaar zijn.
	void ClaimLamps();
	// Huidige aan/uit + dim toepassen op alle geclaimde lampen + emissive-boxen.
	void ApplyToLamps();
	// True als WIJ deze lamp/emissive moeten bezitten: via manual links (indien aanwezig) of anders nabijheid.
	bool OwnsByLinkOrProximity(class UWorld* W, const FVector& Pos) const;
	void ApplyLinkPreview(); // (her)kleur de preview-lampen blauw/normaal op basis van de huidige links

	void Load();
	void Save() const;

	UPROPERTY() TObjectPtr<UStaticMeshComponent> Plate;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Toggle; // uitstekende tuimelknop (rocker) op de plaat
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> PlateMID;

	// Geclaimde lampen (zwak: de mesh/level kan ge-unload worden) + emissive-boxen met hun basis-brightness.
	TArray<TWeakObjectPtr<UPointLightComponent>> Lights;
	TArray<TWeakObjectPtr<UMaterialInstanceDynamic>> Emis;
	TArray<float> EmisBase;
	TArray<FVector> EmisPos; // claim-positie per emissive -> bij her-claim checken of wij nog de dichtstbij zijn

	// Handmatige links: lamp-sleutels (MakeLampKey) die deze schakelaar EXPLICIET bestuurt. Niet leeg =>
	// overschrijft de nabijheids-claim volledig (alleen de gelinkte lampen).
	TSet<FString> LinkedLampKeys;

	// Link-modus preview (tijdelijk): lampen in de buurt die we even hebben overgenomen + hun originele kleur.
	bool bLinkPreview = false;
	TArray<TWeakObjectPtr<class UMaterialInstanceDynamic>> PreviewEmis; // diffuser-MIDs (lightboxes) die we kleuren
	TArray<FVector> PreviewEmisPos;                                     // hun positie -> lamp-sleutel
	TArray<float> PreviewEmisBright;                                   // originele Brightness om te herstellen

	FString PersistKey;
	float ControlRadius = 800.f; // ruim genoeg dat EEN switch een hele kamer (meerdere lampen) dekt; de
	                             // dichtstbijzijnde-switch-check houdt andere kamers (badkamer) apart
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
