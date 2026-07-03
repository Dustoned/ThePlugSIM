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

	// --- UltraDynamicSky-integratie (pack-maps): UDS levert zon/lucht/wolken/weer; wij voeden z'n tijd uit de klok ---
	bool bUseUDS = false;
	TWeakObjectPtr<AActor> UdsSky;          // runtime-gespawnde Ultra_Dynamic_Sky-actor
	FProperty* UdsTimeProp = nullptr;       // "Time of Day" (0..2400)
	UFunction* UdsUpdateFn = nullptr;       // "Update Active Variables"
	UFunction* UdsUpdateStaticFn = nullptr; // "Update Static Variables" (forceert her-cache van o.a. wolken)
	float LastUdsTod = -1.f;
	float LastUdsCloud = -999.f, LastUdsFog = -999.f;
	float UdsExtraNightCloudy = 0.f;         // cloudy-nacht-boost uit
	int32 UdsWeather = 0;                    // 0 clear,1 cloudy,2 rain,3 storm,4 snow,5 fog
	TWeakObjectPtr<AActor> UdsSound;         // environment-sound-actor (tijd/weer-gestuurd)
	TWeakObjectPtr<AActor> UdsWeatherActor;  // Ultra Dynamic Weather (zit in de pack) -> echt regen/sneeuw/storm
	void SpawnUDS();                         // laad + spawn de UDS-BP, cache property/functie
	void DriveUDS(float ClockHour);          // Time of Day uit de klok + Update Active Variables
	void SetUdsDouble(FName P, double V);
	void SetUdsBool(FName P, bool V);
	// Zoals SetUdsDouble maar dan op de UDW-actor (Ultra Dynamic Weather); null-guard + Verbose-log per set
	// zodat we in de log zien of de (versie-afhankelijke) property-naam pakte.
	void SetUdwDouble(FName P, double V);
	void CallUdsUpdate();

	// D24 weer-volume: de settings-slider (WeedUI-categorie 3 = VolWeather) live diffen en op de UDS-sound-actor
	// + UDW-donder toepassen. Defaults 1x cachen bij spawn zodat we altijd vanaf de originele waarde schalen.
	float LastWeatherVol = -1.f;                 // laatst toegepaste categorie-volume (0..1); -1 = nog niet toegepast
	float WeatherVolTimer = 0.f;                 // 0.5s-diff-cadans
	float UdsSoundBaseVolMul = -1.f;             // cached default van de "Volume Multiplier"-BP-var op UdsSound (-1 = onbekend/route dood)
	double UdwCloseThunderBase = -1.0;           // cached default UDW "Close Thunder Volume"
	double UdwDistantThunderBase = -1.0;         // cached default UDW "Distant Thunder Volume"
	void ApplyWeatherVolume(float Vol01);        // schaal de gecachte defaults met Vol01 en push naar de actors

public:
	// UDS-look LIVE tunables (dev-menu Light-tab); UDS bezit de belichting via "Apply Exposure Settings".
	UPROPERTY() float UdsExpDay = -0.5f;       // EV-bias dag (-2..+1)
	UPROPERTY() float UdsExpDawnDusk = -0.3f;  // EV-bias dageraad/schemer
	UPROPERTY() float UdsExpNight = -1.2f;      // EV-bias nacht (-4..+1)
	UPROPERTY() float UdsCloud = 0.4f;         // wolkendek 0..1 (UDS-achtige default = partly cloudy)
	UPROPERTY() float UdsFog = 0.f;            // mist 0..1
	UPROPERTY() float UdsStars = 2.5f;         // sterren-intensiteit
	UPROPERTY() float UdsNebula = 1.2f;        // nebula/melkweg-intensiteit
	UPROPERTY() float UdsNightGlow = 0.3f;     // nacht-lucht-glow (lager = donkerder, sterren poppen)
	void ApplyUdsLook();                       // push exposure + cloud + fog naar UDS (live)
	void SetUdsWeather(int32 WeatherType);     // (oud, ongebruikt) Sky-only weer
	void SetWeatherPreset(const FString& PresetName, double TransitionSeconds = 0.0); // UDW Change Weather (TransitionSeconds=0 -> nette default)
	void SetRandomWeather(bool bOn);                  // auto-weer aan/uit (eigen gewogen loting per dag)
	void PickAndApplyWeather();                       // (server) kiest gewogen een weer + schrijft de index naar WorldSync
	void ApplyWeatherByIndex(int32 Index, float TransitionSeconds); // index (deterministische tabel) -> preset toepassen (server+client)
	bool bAutoWeather = true;                          // auto-weer actief
	float WeatherTimer = 0.f;                           // game-sec tot de volgende weer-keuze (kort na slecht weer)
	// Co-op-sync (H.12): laatst toegepaste weer-index. Server = z'n eigen keuze (niet dubbel toepassen), client =
	// de laatst gelezen server-index (alleen bij wijziging opnieuw toepassen). -1 = nog niets toegepast.
	int32 LastSeenWeatherIndex = -1;
	// WorldSync (op de GameState) 1x resolven + cachen (spiegelt CityDoor::CachedWorldSync); her-resolven zodra invalid.
	mutable TWeakObjectPtr<class UWorldSyncComponent> CachedWorldSync;
	class UWorldSyncComponent* GetWorldSync() const;
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
	TSet<class UDecalComponent*> SeenDecals; // map-decals 1x FadeScreenSize omhoog -> verre decals faden echt uit (perf)
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
	float LightBudgetTimer = 0.f; // licht-budget-pool: cap zichtbare Movable-lampen op de N-dichtstbij de speler (perf: count drijft InitViews/Lighting)
	int32 LightScanDry = 0; // opeenvolgende scans zonder iets nieuws -> scan-interval omhoog (geen 6s-hitch meer)
	TSet<TWeakObjectPtr<AActor>> LightScanSeenActors; // al-verwerkte actors -> volledig overslaan (geen herhaalde
	                                                  // per-actor component-gather + string-checks elke scan = de periodieke hang)
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
