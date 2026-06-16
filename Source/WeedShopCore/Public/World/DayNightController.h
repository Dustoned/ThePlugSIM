// ADayNightController — stuurt de scene-belichting op basis van de dag/nacht-klok (GameState
// DayCycle): roteert + dimt de directional light (zon), dimt de SkyLight mee, en zet een paar
// lantaarnpalen neer die automatisch aangaan zodra het donker wordt. Lokaal/cosmetisch (geen
// replicatie nodig: de klok zelf is al gerepliceerd).

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DayNightController.generated.h"

class UDayCycleComponent;
class ADirectionalLight;
class ASkyLight;
class UPointLightComponent;
class UStaticMeshComponent;
class UMaterialInstanceDynamic;
class APostProcessVolume;

UCLASS()
class WEEDSHOPCORE_API ADayNightController : public AActor
{
	GENERATED_BODY()

public:
	ADayNightController();
	virtual void Tick(float DeltaSeconds) override;

	// De lokale controller (lokaal/cosmetisch gespawnd) -> bereikbaar vanuit de phone-UI om de
	// belichting live te tunen zonder restart.
	static ADayNightController* GetLocal(UWorld* W);
	// FOTO-STAND voor de M-kaart: forceert 1 frame een vaste ochtendzon (geen wit middag-licht,
	// geen nacht-grading, lampen uit) zodat de luchtfoto er ALTIJD hetzelfde uitziet, ongeacht de
	// kloktijd. De eerstvolgende Tick herstelt alles automatisch vanaf de klok.
	void ApplyMapPhotoLight();
	bool HasPackSun() const { return PackSun.IsValid(); }

	// Live-instelbare belichting (sliders in de phone Settings/Test). Defaults = de "huidige" look.
	UPROPERTY() float MoonIntensity = 0.65f;   // nacht: zon-/maanlicht-intensiteit
	UPROPERTY() float SunIntensity  = 6.5f;    // dag: zonlicht-intensiteit
	UPROPERTY() float SkyNight      = 0.85f;   // nacht: skylight-ambient
	UPROPERTY() float SkyDay        = 1.0f;    // dag: skylight-ambient
	UPROPERTY() float MoonPitch     = -52.f;   // nacht: hoek van de hoge maan (graden)
	UPROPERTY() float LampIntensity = 42000.f; // straatlamp-intensiteit (lumens, stad-straatlampen)
	UPROPERTY() float ExposureBias  = 9.f;     // vaste belichtingscompensatie (lager = donkerder)

	// Pack-map (beach) tunables - live via de Light-tab sliders:
	UPROPERTY() float NightGain     = 0.55f;   // nacht-donkerte (color gain schaal, lager = donkerder)
	UPROPERTY() float NightExposure = -1.5f;   // nacht exposure-bias (lager = donkerder nacht)
	UPROPERTY() float DayBloom      = 0.4f;    // dag-gloed (bloom: neon/stoplichten vs zon-waas)
	UPROPERTY() float SunHaze       = 0.002f;  // atmosfeer-nevel rond de zon (Mie)
	float LastAppliedHaze = -1.f;
	// Per-frame licht-loops (dim-lights/skylight/pack-lampen) alleen draaien als de klok-factor of de
	// lamp-slider echt veranderde (+ een 2Hz vangnet voor externe resets) -> scheelt elke frame honderden
	// iteraties zonder zichtbaar verschil.
	float LastLightUpdateMinDayF = -2.f;
	float LastLightUpdateLampI = -1.f;
	float LightUpdateTimer = 0.f;

	// Schrijf de huidige waardes naar Saved/LightConfig.txt (+ log), zodat ze als defaults te bakken zijn.
	void SaveLightConfig() const;
	// Laad Saved/LightConfig.txt terug in de tunables (anders is de Save-knop zinloos).
	void LoadLightConfig();

	// De geadopteerde zon (voor opruimen van extra directional lights in pack-maps).
	class ADirectionalLight* GetSun() const { return Sun.Get(); }
	// Pack-map minimal-modus: stock-look overdag, 's nachts alleen de bestaande lichten dimmen.
	bool IsPackMinimal() const { return bPackMinimal; }
	class ADirectionalLight* GetMoon() const { return Moon.Get(); }
	class ASkyLight* GetSky() const { return Sky.Get(); }
	// Pack-maps: sky (her-)adopteren zodra een gestreamde skylight binnenkomt.
	void TryAdoptSky();

	// --- Lichtschakelaars (APackLightSwitch) ---
	// De schakelaars in je appartement nemen de plafondlampen in hun buurt over van de klok: zo
	// volgen die niet meer de straatlantaarns maar jouw aan/uit + dimmer-stand (per kamer onthouden).
	// Plafondlamp-posities (mesh-origins) van CeilLamp-lampen binnen [Center +/- R] verzamelen.
	void CollectCeilingLightsNear(const FVector& Center, float Radius, TArray<class UPointLightComponent*>& OutLights) const;
	// Emissive-diffusers (de glow-box) binnen [Center +/- R] + hun originele brightness.
	void CollectCeilingEmisNear(const FVector& Center, float Radius, TArray<class UMaterialInstanceDynamic*>& OutMids, TArray<float>& OutBright, TArray<FVector>& OutPos) const;
	// Alle plafondlamp-posities (voor auto-plaatsing van de schakelaars per kamer).
	void GetCeilingLampPositions(TArray<FVector>& Out) const;
	// Lamp/emissive door een schakelaar laten besturen i.p.v. de klok (true) of teruggeven (false).
	void SetSwitchControlledLight(class UPointLightComponent* PL, bool bControlled);
	void SetSwitchControlledEmis(class UMaterialInstanceDynamic* E, bool bControlled);
	// Canonieke "aan"-intensiteit die de klok aan deze lamp zou geven (schakelaar schaalt met dimmer).
	float CeilingOnIntensity(const class UPointLightComponent* PL) const;

protected:
	virtual void BeginPlay() override;

	const UDayCycleComponent* GetDayCycle() const;
	void BuildStreetLamps(const FVector& Center);
	void AddLamp(const FVector& BaseOnGround);
	// Bestaande (felle) binnen-lampen uitzetten en vervangen door een warm, rustig licht.
	void ReplaceIndoorLights();

	UPROPERTY() TObjectPtr<USceneComponent> Root;

	bool bPackMinimal = false;
	TWeakObjectPtr<class APostProcessVolume> NightPPV; // alleen 's nachts gewicht (exposure omlaag)
	TWeakObjectPtr<class ADirectionalLight> PackMoon;  // eigen maan op pack-maps (alleen 's nachts aan)
	TWeakObjectPtr<class ADirectionalLight> PackSun;   // eigen bewegende zon op pack-maps
	TWeakObjectPtr<class APostProcessVolume> BloomPPV; // bloom-rem (zonneschijn was een witte waas)
	bool bAtmosphereTuned = false; // Mie-waas van de map-atmosfeer een keer temmen
	// Pack-lampen: de lantaarn/plafondlamp-meshes van de map hebben geen echte lichten -
	// wij hangen er warme puntlichten aan die op de klok aan/uit gaan.
	UPROPERTY() TArray<TObjectPtr<class UPointLightComponent>> PackLampLights;
	TSet<UStaticMeshComponent*> PackLampSeen;
	// Emissive-diffusers van plafondlampen (MI_Light, 'Brightness'): mee aan/uit dimmen met de lamp,
	// anders blijft de lamp-box wit gloeien terwijl het licht uit is.
	UPROPERTY() TArray<TObjectPtr<class UMaterialInstanceDynamic>> PackCeilEmis;
	TArray<float> PackCeilEmisBright;
	TArray<FVector> PackCeilEmisPos;   // wereld-positie (mesh-origin) per emissive -> matchen aan een kamer/schakelaar
	// Lampen/emissives die door een lichtschakelaar bestuurd worden: de klok-loop slaat ze over.
	TSet<TObjectPtr<class UPointLightComponent>> SwitchControlledLights;
	TSet<TObjectPtr<class UMaterialInstanceDynamic>> SwitchControlledEmis;
	// Minimal-modus: alle gevonden lichten met hun originele intensiteit (dim-factor per klok).
	struct FDimLight { TWeakObjectPtr<class ULightComponent> Light; float OrigIntensity = 0.f; };
	TArray<FDimLight> DimLights;
	// Reflection captures van de map: overdag gebakken -> 's nachts spiegelen ramen/water nog daglicht.
	// Hun brightness mee-dimmen met de klok (0 = nacht) haalt die foute dag-reflectie weg.
	struct FRefCap { TWeakObjectPtr<class UReflectionCaptureComponent> Cap; float OrigBrightness = 1.f; };
	TArray<FRefCap> RefCaps;
	TSet<class UReflectionCaptureComponent*> SeenRefCaps;
	float LastRefMul = -1.f;
	// SKYLIGHTS apart: USkyLightComponent erft van ULightComponentBase (NIET ULightComponent), dus de
	// gewone dim-scan miste 'm -> de movable skylight bleef 's nachts op dag-sterkte en spiegelde z'n
	// hemel-cubemap fel op water/ramen. Hier wel verzameld + gedimd met de klok.
	struct FSkyDim { TWeakObjectPtr<class USkyLightComponent> Sky; float OrigIntensity = 1.f; };
	TArray<FSkyDim> SkyDims;
	TSet<class USkyLightComponent*> SeenSky;
	TArray<TWeakObjectPtr<class UStaticMeshComponent>> DomeComps; // HDRI-fotokoepel (dag-lucht)
	TSet<TWeakObjectPtr<class ULightComponent>> SeenLights;
	float LightScanTimer = 0.f;
	TWeakObjectPtr<ADirectionalLight> Sun;
	TWeakObjectPtr<ADirectionalLight> Moon; // eigen maan-licht: komt op bij zonsondergang, gaat onder bij zonsopkomst
	TWeakObjectPtr<ASkyLight> Sky;

	UPROPERTY() TArray<TObjectPtr<UPointLightComponent>> LampLights;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> LampHeads;
	UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> LampHeadMats;
	TWeakObjectPtr<APostProcessVolume> PPV; // voor live exposure-tuning

	int32 bLampsOn = -1;        // -1 = nog niet gezet
	float LastLampApplied = -1.f; // her-toepassen als de slider de intensiteit wijzigt

	static TWeakObjectPtr<ADayNightController> LocalInstance;
};
