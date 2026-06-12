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

	// Live-instelbare belichting (sliders in de phone Settings/Test). Defaults = de "huidige" look.
	UPROPERTY() float MoonIntensity = 0.65f;   // nacht: zon-/maanlicht-intensiteit
	UPROPERTY() float SunIntensity  = 6.5f;    // dag: zonlicht-intensiteit
	UPROPERTY() float SkyNight      = 0.85f;   // nacht: skylight-ambient
	UPROPERTY() float SkyDay        = 1.0f;    // dag: skylight-ambient
	UPROPERTY() float MoonPitch     = -52.f;   // nacht: hoek van de hoge maan (graden)
	UPROPERTY() float LampIntensity = 42000.f; // straatlamp-intensiteit (lumens, stad-straatlampen)
	UPROPERTY() float ExposureBias  = 9.f;     // vaste belichtingscompensatie (lager = donkerder)

	// Schrijf de huidige waardes naar Saved/LightConfig.txt (+ log), zodat ze als defaults te bakken zijn.
	void SaveLightConfig() const;

	// De geadopteerde zon (voor opruimen van extra directional lights in pack-maps).
	class ADirectionalLight* GetSun() const { return Sun.Get(); }
	// Pack-map minimal-modus: stock-look overdag, 's nachts alleen de bestaande lichten dimmen.
	bool IsPackMinimal() const { return bPackMinimal; }
	class ADirectionalLight* GetMoon() const { return Moon.Get(); }
	class ASkyLight* GetSky() const { return Sky.Get(); }
	// Pack-maps: sky (her-)adopteren zodra een gestreamde skylight binnenkomt.
	void TryAdoptSky();

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
	// Minimal-modus: alle gevonden lichten met hun originele intensiteit (dim-factor per klok).
	struct FDimLight { TWeakObjectPtr<class ULightComponent> Light; float OrigIntensity = 0.f; };
	TArray<FDimLight> DimLights;
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
