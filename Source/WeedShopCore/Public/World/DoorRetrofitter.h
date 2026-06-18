// ADoorRetrofitter - maakt statische deur-bladen in een asset-pack-map (bv. CityBeachStrip) werkend:
// vindt StaticMeshActors met bekende deur-blad-meshes (pivot op het scharnier) en vervangt ze door een
// ACityDoor met datzelfde blad (F = open/dicht, NPC-auto-open, settled-collision). Scant periodiek door
// zodat ook gebouwen die later in-streamen (level instances / world partition) hun deuren krijgen.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "World/CityGenerator.h" // FApartmentHome / FCityPropertyOffer (beach-map woning-registry)
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

	// Kaart: posities van wandelaars ZONDER lichaam (de verren) - de M-kaart toont zo altijd
	// de hele crowd, niet alleen de gematerialiseerde lichamen om de speler heen.
	void GetVirtualWalkerPositions(TArray<FVector>& Out) const;

	// Gemeten huis-box (wand-traces vanaf de thuis-plek): de build-tool laat alleen plaatsen
	// binnen je eigen huis. False zolang de box nog niet betrouwbaar gemeten is.
	bool GetHomeBox(FVector& OutMin, FVector& OutMax) const
	{
		if (!bHomeBoxReady) { return false; }
		OutMin = HomeBoxMin; OutMax = HomeBoxMax; return true;
	}
	// De thuis-plek (altijd gezet via HomeSpawn.txt) -> ruime fallback-bouwzone als de wand-box nog
	// niet betrouwbaar gemeten is, zodat je altijd in je eigen huis kunt bouwen.
	FVector GetHomeAnchor() const { return HomeAnchor; }

	// --- BEACH-MAP WONING-REGISTRY (ROADMAP 4.1) ---
	// Woningen op de pack-map zonder ACityGenerator: index 0 = starter (gratis, al van jou), 1.. =
	// koopbare woningen die de speler met de marker-toets registreert (opgeslagen in BeachHomes.txt).
	const TArray<FApartmentHome>& GetBeachHomes() const { return BeachHomes; }
	void GetBeachPropertyOffers(TArray<FCityPropertyOffer>& Out) const;
	// Marker-tool (dev): registreer de kamer waar de speler NU staat als koopbare woning - meet de
	// wanden, schat een prijs uit de oppervlakte, schrijf naar BeachHomes.txt en herbouw de registry.
	void RegisterHomeAtPlayer(class APawn* Player);
	// (Her)bouw de registry uit de starter-plek + BeachHomes.txt.
	void RebuildBeachHomes();

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
	TSet<TWeakObjectPtr<AActor>> ConvScanRejected; // static-meshes die GEEN blad/glas zijn: 1x checken, daarna skippen (anders elke pass alles opnieuw)
	int32 TotalConverted = 0;
	TArray<TWeakObjectPtr<class ACityDoor>> SpawnedDoors; // om los GLAS aan de juiste deur te hangen
	TSet<TWeakObjectPtr<class UPrimitiveComponent>> GlassFixedComps; // raam-componenten die al geforceerd blokkeren
	// Seen-sets: actors die door een setup-sweep al onderzocht zijn en NIETS (meer) te doen hebben -
	// helemaal overslaan i.p.v. elke 2s opnieuw hun component-arrays bouwen + string-checken (de ~90ms hang).
	TSet<TWeakObjectPtr<AActor>> GlassScanSeen;   // static-meshes die geen los deur-glas zijn
	TSet<TWeakObjectPtr<AActor>> WinFixSeen;      // actors zonder (nog te fixen) glas/raam-collision
	TSet<TWeakObjectPtr<AActor>> LampScanSeen;    // actors zonder binnen-lamp-componenten
	TSet<TWeakObjectPtr<AActor>> ElevScanSeen;    // actors zonder lift-frame/paneel-mesh
	// Adaptieve scan-cadans: stabiel (niks nieuws ingestreamd) -> van 2s naar 8s, scheelt 4x de sweep-hitch.
	int32 ScanIdleStreak = 0;
	bool bScanSlow = false;
	// Wereld 'dirty' = er is een level/cel in/uit-gestreamd sinds de vorige sweep -> 1x zwaar scannen.
	// Staat alles stil, dan slaat de zware ombouw-sweep zichzelf over (geen periodieke freeze).
	bool bWorldDirty = true;
	FVector LastScanPlayerPos = FVector(1e9f); // vangnet: flink verplaatst -> ook dirty (cel mogelijk gestreamd)

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
	int32 LastLockApplyCount = -2;  // handmatige sloten: alleen hertoepassen als het deur-aantal wijzigt (geen elke-pass-lus)
	// Walk-throughs (Saved/NoCollide.txt): elke sessie opnieuw toepassen, ook op gestreamde meshes.
	TArray<FString> NoCollideLines;
	bool bNoCollideLoaded = false;
	// Handmatig vergrendelde deuren (Saved/LockedDoors.txt): slot zonder bewoner-naam.
	TArray<FString> LockedDoorLines;
	bool bLockedDoorsLoaded = false;
	// Deur-snaps (Saved/DoorSnaps.txt): "origX,Y,Z|doelX,Y,Z" - deuren die naast hun kozijn geconverteerd
	// worden, springen elke sessie terug naar het juiste deurvak.
	TArray<FString> DoorSnapLines;
	bool bDoorSnapsLoaded = false;
	int32 LastSnapApplyCount = -2;
	TSet<TWeakObjectPtr<class ACityDoor>> SnappedDoors; // elke deur 1x snappen (geen heen-en-weer-geschuif)
	// Direct-helder glas: binnen elke kamer-job-kolom worden nep-glas materialen METEEN vervangen
	// door het doorzichtige glas, in plaats van te wachten tot de job zelf draait (nabijheid+scans).
	void ApplyInstantGlass();
	// Zoek de stoep/straat op een vaste Y langs de boulevard (down-traces op straat-meshes).
	bool FindStreetPoint(float WorldY, FVector& Out) const;
	TArray<float> PendingSpawnerYs; // boulevard-punten waar nog een klanten-spawner moet komen
	TArray<FVector> PendingSpawnerPoints; // speler-gemarkeerde route-punten (NpcRoute.txt)
	int32 RouteCustomersPerPoint = 4;     // klanten-budget verdeeld over de route-punten
	TArray<TArray<FVector>> NpcRings;     // volledige route-ringen op volgorde (voor de patrouille)
	// Bewoners gespreid laten verschijnen (1 per ~10s), niet allemaal tegelijk.
	struct FPendingResident { TWeakObjectPtr<class ACityDoor> Door; bool bInside = false; };
	TArray<FPendingResident> PendingResidents;
	bool bTowerInvokerPlaced = false; // navmesh-anker in de starter-toren (alle verdiepingen)
	TMap<TWeakObjectPtr<class ACustomerBase>, float> ResidentStuckSince; // boven vast: "lift nemen"-timer
	TArray<FVector> PlacedNavLinks; // automatische trap-naar-straat links (dedupe)
	TArray<TArray<FVector>> NpcChains; // speler-gemarkeerde binnen-kettingen (StairsPath.txt)
	TArray<FVector> LoadedChillSpots;  // hang-plekken (ChillSpots.txt)
	bool bShopsPlaced = false;         // winkels (ShopSpots.txt) eenmalig neerzetten
	bool bFurniturePlaced = false;     // starter-meubels (StarterFurniture.txt) op een verse game
	// Loop-graaf: alle routes/dwarsstraten/oversteken samengeknoopt (knopen + buren).
	TArray<FVector> GraphNodes;
	TArray<TArray<int32>> GraphAdj;
	// VIRTUELE CROWD: 70 wandelaars als data over de graaf (hele stad, altijd); binnen bereik
	// van een speler krijgen ze een echt lichaam, daarbuiten lopen ze als data door.
	struct FVirtualWalker
	{
		FVector Pos = FVector::ZeroVector;
		int32 NextIdx = 0;
		int32 PrevIdx = -1;
		bool bStripLover = false; // blijft het liefst op de main strip (oost-zone)
		TWeakObjectPtr<class ACustomerBase> Body;
	};
	TArray<FVirtualWalker> Crowd;
	// POOL van geparkeerde (verborgen) crowd-lichamen: een ver-weg-lichaam wordt geparkeerd i.p.v. vernietigd
	// en hergebruikt bij her-materialiseren -> elke NPC wordt maar 1x modulair gebouwd (geen rebuild-hitch).
	UPROPERTY() TArray<TObjectPtr<class ACustomerBase>> CrowdPool;
	void TickVirtualCrowd();      // ~0.5s: lichamen materialiseren/opruimen (traces + spawns - DUUR)
	void TickVirtualMove();       // 10x per seconde: vloeiende data-stapjes (alleen rekenwerk)
	int32 CrowdSubTick = 0;       // throttle: TickVirtualCrowd maar 1x per N TickVirtualMove-calls (geen 10Hz-spawn-cascade)
	FTimerHandle CrowdMoveTimer;

	// Balkon-puien op het ECHTE gat in de gevel centreren (gemeten met dwars-traces).
	void FixBalconyPuiPositions();
	TArray<FBox> GlassRects;
	bool bGlassRectsLoaded = false;
	int32 ScanPass = 0;
	// Starter-huis + huur (EUR 500 per 31 dagen, voortgang in Saved/RentState.txt).
	TWeakObjectPtr<class ACityDoor> StarterDoor;
	bool bMovedIntoHome = false;    // (legacy, ongebruikt sinds per-speler HomedPawns)
	TSet<TWeakObjectPtr<APawn>> HomedPawns; // welke speler-pawns al thuisgezet zijn (host + co-op)
	FVector HomeAnchor = FVector::ZeroVector; // thuis-plek (voor het settle-venster)
	float HomeSettleUntil = 0.f;    // absolute cap: pin de speler tot dit moment als de vloer nog niet geladen is
	bool bRoomFloorReady = false;   // true zodra de penthouse-vloer onder de thuis-plek is ingestreamd
	// Beach-map woning-registry (zie boven). Prijzen lopen parallel aan BeachHomes (index 0 = starter = 0).
	TArray<FApartmentHome> BeachHomes;
	TArray<int64> BeachHomePrices;
	bool bBeachHomesBuilt = false;
	// Meet de kamer rond een punt met 4 wand-traces -> halve-afmeting (X/Y). False als <4 wanden binnen bereik.
	bool MeasureRoomHalf(const FVector& Center, FVector& OutHalf) const;
	FVector HomeBoxMin = FVector::ZeroVector; // gemeten huis-grenzen (build-tool restrictie)
	FVector HomeBoxMax = FVector::ZeroVector;
	bool bHomeBoxReady = false;
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
