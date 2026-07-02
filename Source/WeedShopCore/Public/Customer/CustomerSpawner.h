// ACustomerSpawner — spawnt klanten op één punt en stuurt ze naar een plek in de buurt (via de
// navmesh). Server-authoritative. Plaats er één in de map (bij de deur/ingang).

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CustomerSpawner.generated.h"

class ACustomerBase;

UCLASS()
class WEEDSHOPCORE_API ACustomerSpawner : public AActor
{
	GENERATED_BODY()

public:
	ACustomerSpawner();

	// Registry van alle levende spawners (Add in BeginPlay, Remove in EndPlay): hot paths lopen
	// O(instanties) i.p.v. TActorIterator over ALLE actors. Weak-ptrs -> IsValid() checken.
	static const TArray<TWeakObjectPtr<ACustomerSpawner>>& GetAll();

	// Welke klant-klasse gespawnd wordt (leeg = ACustomerBase).
	UPROPERTY(EditAnywhere, Category = "Spawn")
	TSubclassOf<ACustomerBase> CustomerClass;

	UPROPERTY(EditAnywhere, Category = "Spawn")
	int32 MaxCustomers = 50;

	// TEST: spawn slechts ÉÉN klant, met hoge stat (koopklaar), netjes in het centrale park (PlayerStart).
	UPROPERTY(EditAnywhere, Category = "Spawn")
	bool bTestSingleHighStat = false;

	// Bewoners: ken NPC's toe aan appartementen. Ze roamen overdag, gaan 's nachts naar huis, en hun
	// appartementdeur gaat op slot met hun naam. (Heeft voorrang op bovenstaande test-modus.)
	UPROPERTY(EditAnywhere, Category = "Spawn")
	bool bSpawnResidents = true;

	// Hoeveel bewoners ECHT rondlopen (de rest "woont er" via een op-slot-deur met naam). 0 = allemaal.
	UPROPERTY(EditAnywhere, Category = "Spawn")
	int32 MaxResidents = 65;

	// Dagelijkse rotatie: hoeveel fysieke roamers per nieuwe dag wisselen met de virtuele pool, zodat
	// je over een paar dagen iedereen ziet (en het straatbeeld verandert ook als je stilstaat).
	UPROPERTY(EditAnywhere, Category = "Spawn")
	int32 RotatePerDay = 25;

	// 's Nachts loopt er een KLEINERE, (bijna) volledig verslaafde crowd buiten: veel deals mogelijk,
	// maar je heat loopt 's nachts op. Overdag een grotere crowd met minder verslaafden.
	UPROPERTY(EditAnywhere, Category = "Spawn")
	int32 NightRoamers = 25;
	// Addiction-drempel om 's nachts als "verslaafd / op het randje" mee te tellen (= koop-drempel).
	UPROPERTY(EditAnywhere, Category = "Spawn")
	float NightAddictThreshold = 30.f;

	UPROPERTY(EditAnywhere, Category = "Spawn")
	float SpawnInterval = 10.f;

	// Alleen spawnen als er een speler binnen deze afstand is (0 = altijd). Voor streaming-maps:
	// zonder speler in de buurt is de grond daar niet ingeladen en vallen NPC's door de wereld.
	UPROPERTY(EditAnywhere, Category = "Spawn")
	float ActivationRange = 0.f;

	// LOOP-NETWERK (pack-map): alle routes, dwarsstraten en oversteekplekken samengeknoopt tot
	// een graaf. Wandelaars doen een random walk (nooit direct terug waar ze vandaan kwamen),
	// dus kruispunten en oversteken werken vanzelf als verbindingen. Leeg = los slenteren.
	TArray<FVector> NetNodes;
	TArray<TArray<int32>> NetAdj;

	// NAV-DEAD DETECTIE (bv. CityBeachStrip): soms bestaat er wel een NavSystem maar levert de navmesh
	// GEEN paden -> AI->MoveToLocation(bUsePathfinding=true) faalt en de route-wandelaars bevriezen
	// (ze wisselden eindeloos tussen navmesh-poging en directe stap -> stonden stil). 1x getest zodra de
	// ring geladen is; is de navmesh dood, dan lopen we de ring RECHTSTREEKS af (CharacterMovement, dus
	// nog steeds muur/vloer-collision, geen teleport).
	int8 RouteNavState = -1; // -1 = nog niet getest, 0 = navmesh werkt, 1 = navmesh dood (direct lopen)
	bool IsRouteNavDead() const { return RouteNavState == 1; }
	void EnsureRouteNavProbed(class UNavigationSystemV1* Nav);

	// CHILL-PLEKKEN: gemarkeerde hang-plekken; een deel van de wandelaars loopt erheen en
	// blijft daar staan tot de dag wisselt (bezetting wordt gedeeld bijgehouden).
	TArray<FVector> ChillSpots;

	// Een elders gespawnde NPC (bv. bewoner-met-naam) als gewone wandelaar adopteren: zelfde
	// patrouille over de route, zelfde opruim-regels - geen aparte logica. Optioneel met een
	// ENTRY-pad (speler-gemarkeerde ketting, bv. trap naar beneden): die wordt eerst punt-voor-
	// punt afgelopen, daarna voegt de wandelaar in op de normale route-patrouille.
	void AdoptWalker(class ACustomerBase* C, const TArray<FVector>* EntryPath = nullptr);

	// Een EXTERNE teleport (bv. de "lift nemen"-redding in de DoorRetrofitter) heeft deze wandelaar
	// verplaatst: gooi z'n entry-/terugpad weg en laat 'm vanaf de dichtstbijzijnde route-knoop verder
	// patrouilleren - anders liep hij vanaf de straat terug omhoog naar z'n oude ketting-punt.
	void NotifyWalkerTeleported(class ACustomerBase* C);

	// NOOD-pad (kamer-redding in de DoorRetrofitter): zet/overschrijf het entry-pad van deze
	// wandelaar naar dit korte DIRECT-WALK-pad (bv. kamer -> eigen deur -> hal). De patrouille
	// loopt entry-punten rechtstreeks af (geen pathfinding), dus dit werkt ook terwijl de speler
	// kijkt - lopen is geen pop. BEWUST zonder ReturnPath/kringloop: na het pad voegt hij gewoon
	// in op de normale route-patrouille (teruglopen zou 'm de kamer weer in sturen). False =
	// deze wandelaar is niet van deze spawner (en bAdoptIfUnknown staat uit).
	bool ForceEntryPath(class ACustomerBase* C, const TArray<FVector>& Path, bool bAdoptIfUnknown = false);

	// Hoe ver van het spawn-punt de klanten gaan staan.
	UPROPERTY(EditAnywhere, Category = "Spawn")
	float SpotRadius = 350.f;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override; // registry-remove
	virtual void Tick(float DeltaSeconds) override;
	void TrySpawn();
	// Diagnose-monitor (logt of de crowd echt beweegt). Draait alleen na bResidentsSpawned, wat op
	// CityBeachStrip nooit gebeurt - blijft staan als no-op tot het LEGACY bewoners-pad ooit terugkomt.
	void StartResidentMovementMonitor();
	void TickResidentMovementMonitor(float Now);

	// --- Park-wachtrij: iedereen komt 1-2x per dag even bij het park, maar netjes OM DE BEURT. ---
	// Max een paar bewoners tegelijk "op park-trip" (lopen + even blijven); de rest staat in de FIFO-rij en
	// roamt gewoon door tot het hun beurt is. Voorkomt ophoping in het centrum.
	struct FParkTicket { TWeakObjectPtr<ACustomerBase> Holder; float GrantTime = 0.f; };
	TArray<FParkTicket> ParkTickets;                 // actieve trips
	TArray<TWeakObjectPtr<ACustomerBase>> ParkQueue; // wie wacht op z'n beurt (FIFO)

public:
	// True = jouw beurt (ga maar); false = in de rij gezet, roam door en vraag later opnieuw.
	bool RequestParkVisit(ACustomerBase* C);
	// Trip klaar/afgebroken -> beurt vrijgeven voor de volgende.
	void FinishParkVisit(ACustomerBase* C);

protected:

	bool bResidentsSpawned = false;
	FTimerHandle SpawnTimer;
	float NextResidentSpawnTryRealTime = 0.f;
	bool bResidentMonitorActive = false;
	bool bResidentMonitorDone = false;
	float NextResidentMonitorRealTime = 0.f;
	int32 ResidentMonitorSamplesRemaining = 0;

	struct FPatrolState
	{
		int32 NextIdx = 0;
		int32 PrevIdx = -1; // waar hij vandaan kwam (geen U-bochtjes op kruispunten)
		int32 Dir = 1;
		TArray<FVector> Entry; // eerst dit pad aflopen (trap-ketting), daarna de route
		int32 EntryIdx = 0;
		int32 Stall = 0;       // stilstand-teller: na 3x direct lopen i.p.v. pathfinding
		int32 StallRounds = 0; // aantal vastloper-herstel-rondes ZONDER vooruitgang: 2e ronde = klem achter geometrie -> buiten zicht naar de knoop hoppen
		// KRINGLOOP: bewoners met een entry-pad lopen na een tijdje de ketting omgekeerd terug
		// naar binnen en despawnen daar - de woningen-pass zet later een verse bewoner neer.
		TArray<FVector> ReturnPath;
		float PatrolUntil = 0.f; // realtime-moment waarop hij naar huis gaat
		bool bHomeward = false;
		FVector ChillSpot = FVector::ZeroVector; // hang-plek voor vandaag (Zero = geen)
		int32 ChillDay = -1;
	};
	TMap<TWeakObjectPtr<ACustomerBase>, FPatrolState> Patrol; // per wandelaar: volgend route-punt + richting
	int32 LastBurstDay = -1; // dag-start vulling: 1x per dag het quotum in een klap vullen
	// Diagnose: waarom keurt deze spawner kandidaten af? (gelogd zolang het quotum niet vol is)
	int32 RejNav = 0, RejZ = 0, RejStreet = 0, RejView = 0, TryCount = 0;

	UPROPERTY(Transient)
	TArray<TObjectPtr<ACustomerBase>> Spawned;
};
