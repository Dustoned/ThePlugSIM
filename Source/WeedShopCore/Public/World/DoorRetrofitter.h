// ADoorRetrofitter - maakt statische deur-bladen in een asset-pack-map (bv. CityBeachStrip) werkend:
// vindt StaticMeshActors met bekende deur-blad-meshes (pivot op het scharnier) en vervangt ze door een
// ACityDoor met datzelfde blad (F = open/dicht, NPC-auto-open, settled-collision). Scant periodiek door
// zodat ook gebouwen die later in-streamen (level instances / world partition) hun deuren krijgen.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DoorRetrofitter.generated.h"

UCLASS()
class WEEDSHOPCORE_API ADoorRetrofitter : public AActor
{
	GENERATED_BODY()

public:
	ADoorRetrofitter();

	// Top-down kaart-render voor pack-maps (MapWidget-fallback zonder CityGenerator).
	class UTextureRenderTarget2D* GetMapRenderTarget() const { return MapRT; }
	FVector2D GetMapCenter() const { return MapCenter; }
	float GetMapOrthoWidth() const { return MapOrtho; }
	void CaptureMapNow();

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	void ScanAndConvert();

	FTimerHandle ScanTimer;
	FTimerHandle ElevScanTimer;
	FVector ElevScanPos = FVector::ZeroVector;
	bool bElevScan = false;
	void ElevTeleport();
	void ElevDump();
	void SpotDump();
	FTimerHandle SpotScanTimer;
	TSet<TWeakObjectPtr<AActor>> Converted; // originele actors die al een werkende deur kregen
	int32 TotalConverted = 0;
	TArray<TWeakObjectPtr<class ACityDoor>> SpawnedDoors; // om los GLAS aan de juiste deur te hangen
	TSet<TWeakObjectPtr<class UPrimitiveComponent>> GlassFixedComps; // raam-componenten die al geforceerd blokkeren

	// Lift-schachten (per XY-cluster van deurframes): stabiliteits-check + gebouwde schachten.
	TMap<FIntPoint, int32> ElevPrevCount;
	TSet<FIntPoint> ElevBuilt;

	// Kamer-BOUWER: bouwt kamers van losse pack-onderdelen binnen door de speler gemarkeerde
	// rechthoeken (2 opeenvolgende F9-marks = 2 tegenoverliggende hoeken).
	void BuildMarkedRooms();
	TSet<FIntPoint> BuiltRects;

	// Verticale verdieping-kopie: 1 enkele marker = kopieer die verdieping-slice naar alle
	// verdiepingen erboven/eronder (zelfde gebouw), met dedupe (bestaande meshes overslaan).
	void VerticalReplicate();
	void ApplySavedStamps();
	void RefreshStampWindowFixes(); // herhaal-pass voor laat-gestreamde gevel-ramen (idempotent)
	bool bDayMapCaptured = false;   // kaart een keer per sessie schieten (foto-stand maakt tijd irrelevant)
	int32 LastAptDoorCount = -1;    // woningen-slot-pass: opnieuw draaien als er deuren bij komen
	// Starter-huis + huur (EUR 500 per 31 dagen, voortgang in Saved/RentState.txt).
	TWeakObjectPtr<class ACityDoor> StarterDoor;
	bool bMovedIntoHome = false;    // speler 1x per sessie in z'n appartement zetten
	int32 RentDueDay = -1;          // dag-nummer waarop de huur vervalt
	int32 LastRentSeenLeft = -9999; // om 1x per dag te toasten/saven
	bool bWalkersSpawned = false;   // klanten-spawner + nav-invoker een keer plaatsen
	void MakeBakedWindowsReal();    // gebakken kamers: nep-glas -> echt doorzichtig glas, direct bij overlay-load
	bool bBakedWindowsReal = false;
	TSet<FString> AppliedStamps;
	FTimerHandle StampFixT1, StampFixT2;
	bool bStampFixTimersSet = false;
	void RunVertJob(const TArray<FVector>& Marks, const FString& JobId, bool bBakedJob);
	TSet<FString> DoneJobs;                  // afgeronde jobs (deze sessie)
	TWeakObjectPtr<class ULevelStreamingDynamic> BakedOverlay; // gebakken kamers (async geladen)
	TMap<FString, int32> JobLastCount;       // streaming-stabiliteit per job
	TMap<FString, int32> JobStreak;

	// Kamer-kloner: lege deur-slots (deur naar de void) krijgen een kopie van het bron-appartement.
	void CloneRooms();
	TSet<FIntPoint> ClonedRooms; // per deur-slot (pos/100) zodat we niet dubbel klonen
	int32 LastSourceCount = -1; // streaming-stabiliteit van de bron-kamer
	int32 SourceStableStreak = 0;
	int32 CloneLogCooldown = 0;
	bool bFogTamed = false; // basis-scenario-fog 1x getemd (zon-gloed eruit)
	TSet<TWeakObjectPtr<class ULocalLightComponent>> IndoorLightsFixed; // static -> movable gezet

	// Kaart-capture (lazy aangemaakt bij de eerste CaptureMapNow).
	void EnsureMapCapture();
	UPROPERTY() TObjectPtr<class UTextureRenderTarget2D> MapRT;
	UPROPERTY() TObjectPtr<class USceneCaptureComponent2D> MapCapture;
	FVector2D MapCenter = FVector2D::ZeroVector;
	float MapOrtho = 60000.f;
};
