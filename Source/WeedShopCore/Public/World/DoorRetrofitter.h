// ADoorRetrofitter - maakt statische deur-bladen in een asset-pack-map (bv. CityBeachStrip) werkend:
// vindt StaticMeshActors met bekende deur-blad-meshes (pivot op het scharnier) en vervangt ze door een
// ACityDoor met datzelfde blad (F = open/dicht, NPC-auto-open, settled-collision). Scant periodiek door
// zodat ook gebouwen die later in-streamen (level instances / world partition) hun deuren krijgen.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "World/CityTypes.h" // FApartmentHome / FCityPropertyOffer (beach-map woning-registry)
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

	// "Unstuck": dichtstbijzijnde punt op de ECHTE weg in het MIDDEN - de speler-gemarkeerde NPC-route
	// (NpcRings = loop-lijn over het midden van de boulevard); valt terug op de straat-zoeker (FindStreetPoint).
	bool FindNearestRoadPoint(const FVector& From, FVector& Out) const;

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

	// --- COMPETITIVE auto-spiegel (alleen in Competitive co-op) ---
	// Extra bouwbare home-boxes (Apt 603 host + 602 joiner) zodat beide spelers in hun eigen kamer
	// mogen bouwen. Leeg buiten competitive (dan verandert er niets). Door BuildComponent geraadpleegd.
	void GetCompetitiveHomeBoxes(TArray<FBox>& Out) const;
	// Verschoven no-build-zones voor 603 + 602 (uit de solo-kamer). Leeg buiten competitive.
	void GetCompetitiveNoBuildZones(TArray<FBox>& Out) const { Out = CompNoBuildZones; }
	// Bezorg-punt voor de competitive-kamer van deze speler (host of joiner): op de stoep net buiten de
	// dichtstbijzijnde geldige deur van de eigen kamer, of de anchor zelf als er geen deur gevonden is.
	// False zolang de competitive-geometrie nog niet berekend is (bCompHomesReady).
	bool GetCompDeliverySpot(bool bJoiner, FVector& Out) const;

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	void ScanAndConvert();

	// --- GESCATTERDE VINDBARE JOINTS (rond prullenbakken/bankjes + asbakken/kliko's op de beach-strip) ---
	// Server-authoritative: één keer spots verzamelen + tot de cap vullen, daarna periodiek bijvullen
	// als de speler er een oppakt (oppakken vernietigt de pickup -> weak ptr null). Random strain +
	// papier-gewicht + THC% + kwaliteit%, gewogen zodat GOEDE joints (hoge kwaliteit/hoge THC) ZELDZAAM zijn.
	bool bJointsScattered = false;
	TArray<FVector> JointSpots;                                          // gecachete spot-locaties (prullenbak/bankje/asbak/kliko)
	TArray<TWeakObjectPtr<class AWorldItemPickup>> ScatteredJoints;      // levende gescatterde joints
	int32 MaxScatteredJoints = 80;                                      // bovengrens op de map (tunable)
	int32 JointTarget = 0;                                              // huidig doel-aantal (level-geschaald)
	float NewJointSpotAcceptChance = 0.6f;                              // kans dat een NIEUW spot-type (asbak/kliko/container) als spot meetelt -> per-object-kans op een joint blijft LAGER dan bij bankjes/prullenbakken (die tellen altijd)
	float DoubleJointChance = 0.05f;                                    // ~5%: héél af en toe liggen er 2 joints op één spot (zeldzame dubbel-vondst)
	int32 LevelJointTarget() const;                                     // doel-aantal joints o.b.v. speler-level (~12%->60% v/d spots)
	FTimerHandle JointRespawnTimer;
	void ScatterJoints();                                              // one-time: vind spots + vul tot de cap
	void TopUpJoints();                                                // respawn-tick: dode prunen + bijvullen tot cap
	bool PlaceJointAtSpot(const FVector& Spot);                         // 1 joint (+ zeldzaam een 2e) met offset rond Spot
	class AWorldItemPickup* MintJointAt(const FVector& Loc);            // 1 random joint spawnen op Loc

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
	// Zolang de loading-cover op beeld staat juist VERSNELD (0.75s): de sweep-hitches zijn dan onzichtbaar.
	int32 ScanIdleStreak = 0;
	bool bScanSlow = false;
	float ScanRate = 2.0f; // huidige timer-cadans (0.75 cover-op-beeld / 2.0 actief / 8.0 idle)
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
	int32 LastWoningenSig = -1;     // "Woningen op slot"-log alleen bij echte verandering (deur/slot-aantal), niet elke bewoner-churn
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
		int32 RerolledGen = 0; // welke crowd-rotatie-generatie deze walker al heeft (voor de dagelijkse re-skin)
	};
	TArray<FVirtualWalker> Crowd;
	// 's Nachts loopt er een KLEINERE, zwaarder verslaafde crowd buiten (junkies/kopers); overdag de volle 70.
	int32 NightCrowd = 25;
	float NightAddictThreshold = 30.f; // verslaving >= dit = blijft 's nachts buiten (= AddictionToBuy)
	// POOL van geparkeerde (verborgen) crowd-lichamen: een ver-weg-lichaam wordt geparkeerd i.p.v. vernietigd
	// en hergebruikt bij her-materialiseren -> elke NPC wordt maar 1x modulair gebouwd (geen rebuild-hitch).
	UPROPERTY() TArray<TObjectPtr<class ACustomerBase>> CrowdPool;
	UPROPERTY() TArray<TObjectPtr<class ACustomerBase>> CrowdBodies; // STERKE ref naar gematerialiseerde crowd-bodies -> niet-geadopteerde GCen niet weg (anti-churn)
	int32 LastCrowdDay = -1; int32 CrowdRerollGen = 0; // dagelijkse crowd-identiteit-rotatie (off-screen re-skin uit de ~250-pool)
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

	// --- COMPETITIVE auto-spiegel: host -> Apt (starter-100 = 603), joiner -> de buurkamer (starter-101 = 602).
	// Spiegelt de solo-kamer (StarterFurniture.txt + no-build + home-area) naar 603 (kopie) en 602 (echte spiegel)
	// en zet beide spelers in hun eigen kamer. ALLEEN als de game-mode Competitive is; in co-op gebeurt er niets.
	void TickCompetitiveRooms();                                              // berekent geometrie + spiegelt 1x (per scan aangeroepen, self-gated)
	class ACityDoor* FindAptDoor(int32 Number, const FVector& NearXY) const;  // deur met dit appartement-nummer, dichtstbij NearXY
	class ACityDoor* FindNearestDoor(const FVector& P) const;                 // dichtstbijzijnde deur bij een punt (om de kamer-deur te ontgrendelen)
	bool MeasureRoomCenter(const FVector& Near, FVector& OutCenter) const;    // 4 wand-traces -> exact kamer-midden (false als <4 wanden binnen bereik / niet ingestreamd)
	void GetHomeNoBuildZones(const FBox& HomeBox, TArray<FBox>& Out) const;   // no-build-boxen (NoBuildZones.txt) waarvan het centrum binnen HomeBox valt
	AActor* SpawnHomeItem(class UWorld* W, FName ItemId, const FVector& Loc, float Yaw); // 1 meubel spawnen (dispatch = StarterFurniture)
	bool bCompHomesReady = false;     // 603/602 geometrie berekend (anchors + build-boxes + no-build)
	int32 CompDoorWait = 0;           // scans gewacht op de ingestreamde 603/602-deuren (voor de juiste spiegel-as)
	bool bCompMirrorDone = false;     // meubel-spiegel 1x gedaan (verse game)
	bool bCompSwitchesDone = false;   // lichtschakelaars 1x lokaal gespawnd (op ELKE machine - ze repliceren niet)
	bool bComp703LightsOff = false;   // COMPETITIVE: het lege 703-penthouse z'n plafondlampen 1x uitgezet
	FVector CompAnchorHost = FVector::ZeroVector;   // eye-height spawn 603 (host)
	FVector CompAnchorJoiner = FVector::ZeroVector; // eye-height spawn 602 (joiner)
	FBox CompHomeBoxHost = FBox(ForceInit);   // build-box 603
	FBox CompHomeBoxJoiner = FBox(ForceInit); // build-box 602
	FVector CompV603 = FVector::ZeroVector;   // 703 -> 603 verschuiving (furniture-kopie)
	FVector CompMirrorN = FVector::ZeroVector; // spiegelvlak-normaal (603 -> 602, genormaliseerd, Z=0)
	FVector CompMirrorM = FVector::ZeroVector; // spiegelvlak-punt (midden tussen 603 en 602)
	TArray<FBox> CompNoBuildZones;            // verschoven no-build-boxen voor 603 + 602
	TSet<TWeakObjectPtr<APawn>> CompHomedPawns; // spelers die al naar hun competitive-kamer gezet zijn
	int32 CompDiagCount = 0;          // TIJDELIJK: tel diagnose-logs (waarom vuurt competitive niet)
	int32 CompDiag3Count = 0;         // TIJDELIJK: stap-3 (speler-plaatsing) diagnose
	TWeakObjectPtr<class ACityDoor> CompDoorHost;   // 603-deur (unlock -> speler-kamer i.p.v. NPC-bewoner)
	TWeakObjectPtr<class ACityDoor> CompDoorJoiner; // 602-deur
	TSet<TWeakObjectPtr<class ACityDoor>> CompClaimedDoors; // alle deuren die nu door competitive geclaimd zijn (voor loslaten als ze niet meer bij een kamer horen)
	FVector Comp703Anchor = FVector::ZeroVector;    // de ORIGINELE 703-thuis-plek (bewaard voordat HomeAnchor naar de host-marker overschreven wordt) - referentie voor de meubel-kopie
	void GetCompetitiveMarkers(TArray<FVector>& Out) const; // de 2 spawn-markers (CompSpawns.txt, anders de laatste 2 F9-markers)
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
