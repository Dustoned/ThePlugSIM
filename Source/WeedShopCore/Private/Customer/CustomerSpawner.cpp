#include "Customer/CustomerSpawner.h"
#include "AIController.h"

#include "WeedShopCore.h"
#include "Customer/CustomerBase.h"
#include "World/DayCycleComponent.h"
#include "Game/WeedShopGameState.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"

// Aantal WANDELAARS (ambient crowd, GEEN bewoners) in de Spawned-lijst. De spawn/cull-caps gaan over de
// wandelaar-crowd; bewoners (Resident_-id) zitten OOK in Spawned maar tellen NIET mee - anders is de cap
// altijd overschreden door de ~65 bewoners (= er spawnen nooit ambient-wandelaars + alles wordt ge-culld).
// LET OP: op NpcId checken (IsResidentWalker), niet op IsResident() - bResident wordt sinds de
// vereenvoudiging nooit meer gezet, dus bewoners telden ONBEDOELD als roamers mee (en route-spawners
// hebben MaxCustomers=0 -> elke geadopteerde bewoner werd off-screen meteen weggeculld).
static int32 CountRoamers(const TArray<TObjectPtr<ACustomerBase>>& Arr)
{
	int32 N = 0;
	for (const TObjectPtr<ACustomerBase>& C : Arr)
	{
		if (IsValid(C) && !C->IsResidentWalker()) { ++N; }
	}
	return N;
}

// Statische registry van alle levende spawners (zie GetAll in de header): gevuld in BeginPlay,
// geleegd in EndPlay. Hot paths (DoorRetrofitter-reddingen) lopen hierdoor O(instanties).
static TArray<TWeakObjectPtr<ACustomerSpawner>> GSpawnerRegistry;
const TArray<TWeakObjectPtr<ACustomerSpawner>>& ACustomerSpawner::GetAll() { return GSpawnerRegistry; }

ACustomerSpawner::ACustomerSpawner()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bTickEvenWhenPaused = true;
	// ZONDER root-component heeft een runtime-gespawnde spawner GEEN transform: GetActorLocation()
	// is dan altijd (0,0,0) - alle klanten spawnden daardoor op de oorsprong onder het dek.
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

namespace
{
	// Gedeelde chill-bezetting (over alle spawners): plek-sleutel -> wie er staat/heen loopt.
	TMap<FIntVector, TWeakObjectPtr<ACustomerBase>> GChillTaken;
	FIntVector ChillKey(const FVector& P) { return FIntVector(FMath::RoundToInt(P.X / 50.f), FMath::RoundToInt(P.Y / 50.f), 0); }
}

namespace
{
	// STRAAT-CHECK: een plek telt alleen als de grond eronder een straat/stoep-mesh is
	// (SM_Street, SM_BlackStreet, Sidewalk, Road, ...). Voorkomt spawnen/mikken bovenop
	// containers, tafels en andere props - preventief, dus geen teleport-redders nodig.
	bool IsOnStreetSurface(UWorld* W, const FVector& P)
	{
		if (!W) { return false; }
		FHitResult H;
		if (!W->LineTraceSingleByChannel(H, P + FVector(0.f, 0.f, 250.f), P - FVector(0.f, 0.f, 250.f), ECC_Visibility)) { return false; }
		const UStaticMeshComponent* SMC = Cast<UStaticMeshComponent>(H.GetComponent());
		if (!SMC || !SMC->GetStaticMesh()) { return false; }
		const FString Nm = SMC->GetStaticMesh()->GetName();
		return Nm.Contains(TEXT("Street")) || Nm.Contains(TEXT("Sidewalk")) || Nm.Contains(TEXT("Road"))
			|| Nm.Contains(TEXT("ConcretePath")) || Nm.Contains(TEXT("Pavement")) || Nm.Contains(TEXT("Boardwalk"))
			|| Nm.Contains(TEXT("Crosswalk")) || Nm.Contains(TEXT("Floor"));
	}

	// Goedkope wandelaar: animatie alleen updaten als hij in beeld is + URO (lagere anim-rate
	// op afstand). Nodig om ~70 NPC's te kunnen draaien zonder de framerate te slopen.
	void MakeWalkerCheap(ACustomerBase* C)
	{
		if (USkeletalMeshComponent* Mesh = C->GetMesh())
		{
			Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
			Mesh->bEnableUpdateRateOptimizations = true;
		}
		// RVO-ontwijking: wandelaars stappen netjes om elkaar (en de speler) heen in plaats
		// van elkaar eeuwig klem te duwen bij frontale ontmoetingen.
		if (UCharacterMovementComponent* Mv = C->GetCharacterMovement())
		{
			Mv->SetAvoidanceEnabled(true);
			Mv->AvoidanceConsiderationRadius = 140.f; // pas dichtbij uitwijken -> geen grote persoonlijke zone (was 400)
			Mv->AvoidanceWeight = 0.4f;               // zachter aan de kant sliden, niet hard wegduwen (was 0.75)
		}
	}
}

void ACustomerSpawner::AdoptWalker(ACustomerBase* C, const TArray<FVector>* EntryPath)
{
	if (!C) { return; }
	Spawned.Add(C);
	MakeWalkerCheap(C);
	if (UCharacterMovementComponent* Mv = C->GetCharacterMovement()) { Mv->MaxWalkSpeed = 165.f; }
	FPatrolState St;
	if (EntryPath && EntryPath->Num() >= 2)
	{
		St.Entry = *EntryPath;
		// Terugweg = dezelfde ketting omgekeerd; na 3-7 minuten route gaat hij naar huis.
		for (int32 ri = EntryPath->Num() - 1; ri >= 0; --ri) { St.ReturnPath.Add((*EntryPath)[ri]); }
		St.PatrolUntil = (GetWorld() ? GetWorld()->GetRealTimeSeconds() : 0.f) + FMath::FRandRange(180.f, 420.f);
	}
	if (NetNodes.Num() >= 2)
	{
		const FVector RefLoc = St.Entry.Num() > 0 ? St.Entry.Last() : C->GetActorLocation();
		float BD = TNumericLimits<float>::Max();
		for (int32 ri = 0; ri < NetNodes.Num(); ++ri)
		{
			const float Dd = FVector::DistSquared2D(NetNodes[ri], RefLoc);
			if (Dd < BD) { BD = Dd; St.NextIdx = ri; }
		}
		St.PrevIdx = -1;
	}
	// CHILL-toewijzing: ~40% van de wandelaars pakt een vrije hang-plek in de buurt en blijft
	// daar vandaag staan (na een eventueel entry-pad eerst).
	if (ChillSpots.Num() > 0 && FMath::FRand() < 0.4f)
	{
		const FVector RefLoc = St.Entry.Num() > 0 ? St.Entry.Last() : C->GetActorLocation();
		float BD = TNumericLimits<float>::Max();
		FVector Best = FVector::ZeroVector;
		for (const FVector& Sp : ChillSpots)
		{
			const TWeakObjectPtr<ACustomerBase>* Taken = GChillTaken.Find(ChillKey(Sp));
			if (Taken && Taken->IsValid()) { continue; } // bezet
			const float Dd = FVector::DistSquared2D(Sp, RefLoc);
			if (Dd < BD && Dd < 12000.f * 12000.f) { BD = Dd; Best = Sp; }
		}
		if (!Best.IsNearlyZero())
		{
			St.ChillSpot = Best;
			GChillTaken.Add(ChillKey(Best), C);
			if (UWorld* Wd = GetWorld())
			{
				if (AWeedShopGameState* GSd = Wd->GetGameState<AWeedShopGameState>())
				{
					if (GSd->GetDayCycle()) { St.ChillDay = GSd->GetDayCycle()->GetDayNumber(); }
				}
			}
		}
	}
	Patrol.Add(C, St);
}

void ACustomerSpawner::NotifyWalkerTeleported(ACustomerBase* C)
{
	if (!C) { return; }
	FPatrolState* St = Patrol.Find(C);
	if (!St) { return; }
	// Extern verplaatst (bv. "lift genomen" naar de straat): het oude entry-/terugpad is niet meer
	// relevant - zonder dit liep hij vanaf de straat terug omhoog naar z'n oude ketting-punt.
	St->Entry.Reset();
	St->EntryIdx = 0;
	St->ReturnPath.Reset();
	St->bHomeward = false;
	St->Stall = 0;
	St->StallRounds = 0;
	if (NetNodes.Num() >= 2)
	{
		float BD = TNumericLimits<float>::Max();
		for (int32 ri = 0; ri < NetNodes.Num(); ++ri)
		{
			const float Dd = FVector::DistSquared2D(NetNodes[ri], C->GetActorLocation());
			if (Dd < BD) { BD = Dd; St->NextIdx = ri; }
		}
		St->PrevIdx = -1;
	}
}

bool ACustomerSpawner::ForceEntryPath(ACustomerBase* C, const TArray<FVector>& Path, bool bAdoptIfUnknown)
{
	if (!C || Path.Num() < 1) { return false; }
	if (!Patrol.Contains(C))
	{
		if (!bAdoptIfUnknown) { return false; }
		AdoptWalker(C); // ZONDER EntryPath adopteren: het nood-pad hieronder mag geen ReturnPath/kringloop krijgen
	}
	FPatrolState& St = Patrol.FindOrAdd(C);
	St.Entry = Path;
	St.EntryIdx = 0;
	St.Stall = 0;
	St.StallRounds = 0;
	St.bHomeward = false;
	St.ReturnPath.Reset(); // nood-pad NIET later omgekeerd teruglopen (dat is de kamer weer in)
	return true;
}

void ACustomerSpawner::BeginPlay()
{
	Super::BeginPlay();
	GSpawnerRegistry.Add(this);
	if (UWorld* World = GetWorld())
	{
		NextResidentSpawnTryRealTime = World->GetRealTimeSeconds() + 0.5f;
	}
	if (!IsNetMode(NM_Client)) // CO-OP: spawner is per-proces + niet-gerepliceerd -> HasAuthority() is OOK true op de joiner; gebruik de wereld-netmode
	{
		// Snel retry-interval zodat de bewoners verschijnen zodra de stad gebouwd is.
		GetWorldTimerManager().SetTimer(SpawnTimer, this, &ACustomerSpawner::TrySpawn, 1.0f, true, 1.0f);
	}
}

void ACustomerSpawner::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GSpawnerRegistry.Remove(this);
	Super::EndPlay(EndPlayReason);
}

void ACustomerSpawner::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UWorld* World = GetWorld();
	if (!World || IsNetMode(NM_Client))
	{
		return;
	}
	if (bResidentsSpawned)
	{
		// SpawnTimer bewust NIET stoppen: TrySpawn moet blijven draaien voor doorlopende wandelaar-aanvulling
		// + cull + de route-patrouille (die ALLE Spawned laat lopen). Zonder dit staat de hele crowd stil
		// zodra de bewoners gespawnd zijn -> lege/stilstaande straat.
		if (!bResidentMonitorActive && !bResidentMonitorDone)
		{
			StartResidentMovementMonitor();
		}
		TickResidentMovementMonitor(World->GetRealTimeSeconds());
		if (!bResidentMonitorActive)
		{
			SetActorTickEnabled(false);
		}
		return;
	}

	const float Now = World->GetRealTimeSeconds();
	if (Now < NextResidentSpawnTryRealTime)
	{
		return;
	}

	NextResidentSpawnTryRealTime = Now + 2.f;
	TrySpawn();
}

void ACustomerSpawner::StartResidentMovementMonitor()
{
	if (bResidentMonitorActive || bResidentMonitorDone)
	{
		return;
	}
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	bResidentMonitorActive = true;
	ResidentMonitorSamplesRemaining = 12;
	NextResidentMonitorRealTime = World->GetRealTimeSeconds() + 6.f;
	SetActorTickEnabled(true);
}

void ACustomerSpawner::TickResidentMovementMonitor(float Now)
{
	if (!bResidentMonitorActive || Now < NextResidentMonitorRealTime)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		bResidentMonitorActive = false;
		bResidentMonitorDone = true;
		return;
	}
	if (World->IsPaused())
	{
		UE_LOG(LogWeedShop, Log, TEXT("Resident movement monitor: paused=1 waiting for unpaused gameplay"));
		NextResidentMonitorRealTime = Now + 6.f;
		return;
	}

	const FVector CityCenter = FVector::ZeroVector;
	const float CellSize = 3000.f;

	int32 Total = 0;
	int32 Visible = 0;
	int32 Outdoor = 0;
	int32 Emerging = 0;
	int32 Entering = 0;
	int32 Moving = 0;
	int32 WithGoal = 0;
	int32 ParkActive = 0;
	int32 ParkDoneToday = 0;
	int32 ParkNeedsToday = 0;
	int32 OffSidewalk = 0;
	int32 CrossingStreet = 0;
	int32 StuckSuspect = 0;
	int32 NearEdge = 0;
	int32 IssueCount = 0;
	TArray<FString> IssueDetails;
	IssueDetails.Reserve(6);
	TSet<FIntPoint> OccupiedCells;

	for (TActorIterator<ACustomerBase> It(World); It; ++It)
	{
		ACustomerBase* Resident = *It;
		if (!IsValid(Resident) || !Resident->IsResident())
		{
			continue;
		}

		FResidentMovementSnapshot Snapshot;
		if (!Resident->GetResidentMovementSnapshot(Snapshot))
		{
			continue;
		}

		++Total;
		if (Snapshot.bVisibleOnMap) { ++Visible; }
		if (!Snapshot.bAtHomeInside) { ++Outdoor; }
		if (Snapshot.bEmergingFromHome) { ++Emerging; }
		if (Snapshot.bEnteringHome) { ++Entering; }
		if (Snapshot.Speed2D > 12.f) { ++Moving; }
		if (Snapshot.bHasGoal) { ++WithGoal; }
		if (Snapshot.bGoalIsPark || Snapshot.bParkPause) { ++ParkActive; }
		if (Snapshot.bNeedsParkVisitToday) { ++ParkNeedsToday; } else { ++ParkDoneToday; }
		if (!Snapshot.bOnSidewalkOrPark) { ++OffSidewalk; }
		if (Snapshot.bLikelyStreetCrossing) { ++CrossingStreet; }
		if (Snapshot.bStuckSuspect) { ++StuckSuspect; }
		if (Snapshot.bNearMapEdge) { ++NearEdge; }
		const bool bNoOutdoorGoal = !Snapshot.bAtHomeInside && !Snapshot.bEmergingFromHome && !Snapshot.bEnteringHome
			&& !Snapshot.bHasGoal && Snapshot.NoGoalSeconds > 6.f;
		const bool bParkUrgentWithoutParkGoal = Snapshot.bParkUrgentToday && !Snapshot.bGoalIsPark && !Snapshot.bParkPause;
		if ((!Snapshot.bOnSidewalkOrPark && !Snapshot.bLikelyStreetCrossing) || Snapshot.bStuckSuspect || bNoOutdoorGoal || bParkUrgentWithoutParkGoal)
		{
			++IssueCount;
			if (IssueDetails.Num() < 6)
			{
				FString Flags;
				if (!Snapshot.bOnSidewalkOrPark && !Snapshot.bLikelyStreetCrossing) { Flags += TEXT("offSidewalk,"); }
				if (Snapshot.bStuckSuspect) { Flags += TEXT("stuck,"); }
				if (bNoOutdoorGoal) { Flags += TEXT("noGoal,"); }
				if (bParkUrgentWithoutParkGoal) { Flags += TEXT("parkUrgent,"); }
				if (Flags.EndsWith(TEXT(","))) { Flags.LeftChopInline(1); }
				IssueDetails.Add(FString::Printf(
					TEXT("%s[%s] loc=(%.0f,%.0f) goal=(%.0f,%.0f) dist=%.0f speed=%.0f stuck=%.1f noGoal=%.1f"),
					*Snapshot.ResidentLabel, *Flags,
					Snapshot.Location.X, Snapshot.Location.Y,
					Snapshot.Goal.X, Snapshot.Goal.Y,
					Snapshot.DistanceToGoal, Snapshot.Speed2D,
					Snapshot.StuckSeconds, Snapshot.NoGoalSeconds));
			}
		}
		if (Snapshot.bVisibleOnMap)
		{
			OccupiedCells.Add(FIntPoint(
				FMath::FloorToInt((Snapshot.Location.X - CityCenter.X) / CellSize),
				FMath::FloorToInt((Snapshot.Location.Y - CityCenter.Y) / CellSize)));
		}
	}

	const int32 SampleIndex = 13 - ResidentMonitorSamplesRemaining;
	UE_LOG(LogWeedShop, Log,
		TEXT("Resident movement monitor: sample=%d total=%d visible=%d outdoor=%d emerging=%d entering=%d moving=%d goals=%d parkActive=%d parkDoneToday=%d parkNeedsToday=%d offSidewalk=%d crossing=%d stuck=%d nearEdge=%d cells=%d"),
		SampleIndex, Total, Visible, Outdoor, Emerging, Entering, Moving, WithGoal, ParkActive, ParkDoneToday, ParkNeedsToday,
		OffSidewalk, CrossingStreet, StuckSuspect, NearEdge, OccupiedCells.Num());
	if (IssueCount > 0)
	{
		UE_LOG(LogWeedShop, Warning,
			TEXT("Resident movement monitor issues: sample=%d count=%d details=%s"),
			SampleIndex, IssueCount, *FString::Join(IssueDetails, TEXT(" | ")));
	}

	--ResidentMonitorSamplesRemaining;
	NextResidentMonitorRealTime = Now + 12.f;
	if (ResidentMonitorSamplesRemaining <= 0)
	{
		bResidentMonitorActive = false;
		bResidentMonitorDone = true;
	}
}

void ACustomerSpawner::EnsureRouteNavProbed(UNavigationSystemV1* Nav)
{
	if (RouteNavState >= 0) { return; }          // al getest (1x, gecachet)
	if (NetNodes.Num() < 2 || !Nav) { return; }  // ring nog niet geladen -> volgende tik opnieuw proberen
	// Probeer een ECHT pad tussen twee naburige ring-knopen. Lukt dat niet, dan levert de navmesh hier
	// geen paden -> we lopen de ring daarna RECHTSTREEKS af (geen pathfinding) i.p.v. te bevriezen.
	int32 A = 0, B = 1;
	for (int32 i = 0; i < NetAdj.Num(); ++i)
	{
		if (NetAdj[i].Num() > 0) { A = i; B = NetAdj[i][0]; break; }
	}
	UNavigationPath* P = UNavigationSystemV1::FindPathToLocationSynchronously(GetWorld(), NetNodes[A], NetNodes[B], nullptr);
	const bool bOk = P && P->IsValid() && P->PathPoints.Num() > 1;
	RouteNavState = bOk ? 0 : 1;
	UE_LOG(LogWeedShop, Verbose, TEXT("Route-spawner nav-probe: %s (knoop %d->%d)"), // Verbose: 37x bij opstart, bedoelde nav-dead fallback (geen fout)
		bOk ? TEXT("navmesh OK") : TEXT("NAVMESH DOOD -> direct ring-walk"), A, B);
}

void ACustomerSpawner::TrySpawn()
{
	UWorld* World = GetWorld();
	// CO-OP: server-authoritative. Alle ACustomerBase-spawns hieronder (patrouille-walkers) draaien alleen
	// op de server; de joiner krijgt de bodies via replicatie (ACustomerBase repliceert nu). De spawner
	// beweegt de bodies server-side, de positie repliceert via SetReplicateMovement.
	if (!World || IsNetMode(NM_Client)) { return; }
	// Streaming-gate: zonder speler in de buurt is de grond hier mogelijk niet ingeladen -
	// dan zakken verse NPC's door de wereld. Even wachten tot iemand dichtbij komt.
	if (ActivationRange > 0.f)
	{
		bool bNear = false;
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			const APawn* P = It->Get() ? It->Get()->GetPawn() : nullptr;
			if (P && FVector::Dist2D(P->GetActorLocation(), GetActorLocation()) < ActivationRange) { bNear = true; break; }
		}
		if (!bNear) { return; }
	}

	// Verlopen klanten opruimen.
	Spawned.RemoveAll([](const TObjectPtr<ACustomerBase>& C) { return !IsValid(C); });

	// LOSSE WANDELAARS (pack-map, bSpawnResidents=false): het bewoners-systeem vereist de
	// CityGenerator en die bestaat hier niet - dus spawnen we gewone klanten rond dit punt.
	// De navmesh-projectie heeft een KRAPPE Z-marge: bestaat er hier alleen navmesh op het
	// onderniveau (onder het dek), dan spawnen we NIET in plaats van eronder.
	if (!bSpawnResidents)
	{
		UNavigationSystemV1* Nav = UNavigationSystemV1::GetCurrent(World);
		if (!Nav) { return; }
		EnsureRouteNavProbed(Nav);
		const bool bDirectRing = IsRouteNavDead(); // dode navmesh -> ring RECHTSTREEKS aflopen (geen pathfinding)
		// Hoogte-marge 90cm: de spawner-actor staat zelf 60cm boven het marker-punt, dus 50cm
		// keurde ALLES af (straat ligt altijd ~60cm onder de spawner). Het service-niveau
		// (~200cm lager) valt nog steeds buiten de marge, en props (containers/tafels) worden
		// nu door de STRAAT-NAAM-check geweerd, niet meer door de hoogte.
		const float ZTol = 90.f;
		// Dag/nacht-doelaantal voor de crowd: overdag VOL (MaxCustomers), 's nachts dunt het uit naar alleen
		// "junkies" (een kwart). Overtollige NPC's worden ALLEEN BUITEN ZICHT opgeruimd, nooit voor je neus.
		int32 NightOrDayTarget = MaxCustomers;
		if (AWeedShopGameState* GSnt = World->GetGameState<AWeedShopGameState>())
		{
			if (auto* DCnt = GSnt->GetDayCycle())
			{
				if (DCnt->IsNight()) { NightOrDayTarget = FMath::Max(2, NightRoamers); } // 's nachts de nacht-crowd (junkies)
			}
		}
		// Opruimen: wandelaars die GEZAKT zijn (onder de stoep) of ergens OP geklommen staan.
		// Hoogte vergelijken met het DICHTSTBIJZIJNDE route-punt (de route heeft verloop - tegen
		// de eigen spawner meten doodde wandelaars die gewoon de ring afliepen). Bewoners
		// (Resident_-id) zijn vrijgesteld: die zijn legitiem binnen/boven (trap, verdiepingen).
		// Afstands-throttle: NPC's ver van alle spelers (120m+) tikken op halve snelheid - je
		// ziet ze toch nauwelijks, en dit halveert de CPU-kosten van de verre helft van de crowd.
		for (int32 wi = Spawned.Num() - 1; wi >= 0; --wi)
		{
			ACustomerBase* Cw0 = Spawned[wi];
			if (!IsValid(Cw0)) { continue; }

			// Afstand tot de DICHTSTBIJZIJNDE speler -> bepaalt de tick-throttle EN of we uberhaupt mogen despawnen.
			float MinPd = TNumericLimits<float>::Max();
			for (FConstPlayerControllerIterator PIt = World->GetPlayerControllerIterator(); PIt; ++PIt)
			{
				const APawn* Pp = PIt->Get() ? PIt->Get()->GetPawn() : nullptr;
				if (Pp) { MinPd = FMath::Min(MinPd, FVector::Dist2D(Pp->GetActorLocation(), Cw0->GetActorLocation())); }
			}
			const bool bFar = MinPd > 12000.f;
			Cw0->SetActorTickInterval(bFar ? 0.4f : 0.f);
			if (UCharacterMovementComponent* Mv0 = Cw0->GetCharacterMovement())
			{
				Mv0->SetComponentTickInterval(bFar ? 0.15f : 0.f);
				// RVO-avoidance (per-tick buren berekenen) uit voor verre NPC's (>120m, meestal off-screen):
				// pure CPU-winst, geen zichtbaar verschil - ze hoeven daar niet netjes om elkaar te lopen.
				if (Mv0->bUseRVOAvoidance == bFar) { Mv0->SetAvoidanceEnabled(!bFar); }
			}

			// Bewoner-vrijstelling op NpcId (IsResidentWalker): met IsResident() was deze vrijstelling dood
			// (bResident wordt nooit gezet) -> bewoners op hun entry-ketting (hal/trap, ver boven straat-Z)
			// werden door de Dz-strikes en de MaxCustomers=0-cap opgeruimd terwijl ze gewoon onderweg waren.
			if (Cw0->IsResidentWalker()) { continue; }

			// KERN-REGEL: NOOIT voor de neus van de speler despawnen. Binnen ~80m verdwijnt er niks -> strikes
			// resetten en door. Alleen BUITEN ZICHT (off-screen) ruimen we op. Zo zie je nooit meer een NPC
			// midden op straat wegpoffen.
			if (MinPd < 8000.f) { Cw0->DespawnStrikes = 0; continue; }

			// Off-screen + TE VEEL volk -> opruimen tot het dag/nacht-doelaantal. Overdag is het doel vol, dus
			// dit doet niets; 's nachts zakt het doel -> de crowd dunt off-screen vanzelf uit naar 'junkies'.
			// MAAR: de DoorRetrofitter-virtuele-crowd (blijvende lichamen, BodyCap=70) NOOIT cullen - die beheert
			// z'n eigen aantal; cullden we ze, dan re-materialiseerde de DoorRetrofitter ze meteen elders (= de churn).
			if (!Cw0->bVirtualCrowdBody && CountRoamers(Spawned) > NightOrDayTarget)
			{
				Cw0->Destroy();
				Spawned.RemoveAt(wi);
				continue;
			}

			// Off-screen + ECHT vastgelopen (stilstaand, ver onder/boven de route = door de map gezakt of op een
			// prop geklommen) -> pas opruimen na ~30s. Speler-regel: NPC's zijn er ALTIJD; alleen eentje die
			// echt LANG off-screen vastzit mag weg (re-materialiseert dan elders = "weg-tp"). Loop draait op 1Hz.
			const FVector L0 = Cw0->GetActorLocation();
			float RefZ = GetActorLocation().Z;
			if (NetNodes.Num() >= 2)
			{
				float BD0 = TNumericLimits<float>::Max();
				for (const FVector& RPt : NetNodes)
				{
					const float Dd0 = FVector::DistSquared2D(RPt, L0);
					if (Dd0 < BD0) { BD0 = Dd0; RefZ = RPt.Z; }
				}
			}
			const float Dz = L0.Z - RefZ;
			const bool bWalking = Cw0->GetVelocity().SizeSquared2D() > 25.f * 25.f;
			if (!Cw0->bVirtualCrowdBody && !bWalking && (Dz < -250.f || Dz > 450.f)) // crowd-bodies ook off-screen NOOIT cullen (anti-churn pariteit)
			{
				if (++Cw0->DespawnStrikes >= 30) // ~30s continu off-screen + vast voordat 'ie weggaat (was 2s)
				{
					Cw0->Destroy();
					Spawned.RemoveAt(wi);
				}
			}
			else
			{
				Cw0->DespawnStrikes = 0;
			}
		}
		// ROUTE-PATROUILLE: wandelaars lopen de gemarkeerde ring punt-voor-punt af. Wie bij z'n
		// punt is (of stilstaat) krijgt het volgende punt, met wat zijwaartse variatie zodat het
		// geen ganzenmars wordt. Klanten die op de speler wachten (deal) blijven met rust.
		if (NetNodes.Num() >= 2)
		{
			for (const TObjectPtr<ACustomerBase>& Cw : Spawned)
			{
				if (!IsValid(Cw)) { continue; }
				// PRAAT-PAUZE: ALLEEN tijdens echte interactie (deal-HUD open met deze klant)
				// staat hij stil - daarna pakt hij z'n wandeling direct weer op. Dichtbij staan
				// alleen is geen reden om te stoppen.
				// CO-OP: stop OOK op de gerepliceerde bTalkingToPlayer (server-authoritatief gezet zodra EEN speler het
				// gesprek opent), niet alleen op de client-lokale ConversationHoldUntil. Anders blijft de server-route-
				// patrouille de NPC voortduwen als de JOINER praat (ConversationHoldUntil staat dan alleen op de joiner-
				// copy) -> de NPC vecht met de deal-stop = 'blijft proberen door te lopen' op de host.
				if (Cw->bTalkingToPlayer || World->GetRealTimeSeconds() < Cw->ConversationHoldUntil)
				{
					if (AAIController* AIp = Cast<AAIController>(Cw->GetController())) { AIp->StopMovement(); }
					continue;
				}
				FPatrolState& St = Patrol.FindOrAdd(Cw);
				const FVector Cur = Cw->GetActorLocation();
				// ENTRY-pad eerst (speler-gemarkeerde ketting, bv. de trap af): punt-voor-punt,
				// en blijft hij steken dan loopt hij het segment RECHTSTREEKS (geen pathfinding) -
				// de ketting-punten van de speler zijn fysiek beloopbaar, ook over de trap.
				if (St.Entry.Num() > 0 && St.EntryIdx < St.Entry.Num())
				{
					const FVector Tgt = St.Entry[St.EntryIdx];
					if (FVector::Dist(Cur, Tgt) < 170.f)
					{
						++St.EntryIdx;
						St.Stall = 0;
						// Thuisgekomen (terugweg helemaal afgelegd): binnen despawnen - de
						// woningen-pass zet later een verse bewoner neer (kringloop).
						if (St.bHomeward && St.EntryIdx >= St.Entry.Num() && !Cw->bVirtualCrowdBody)
						{
							Cw->Destroy();
						}
						continue;
					}
					if (Cw->GetVelocity().SizeSquared2D() > 25.f) { St.Stall = 0; continue; }
					++St.Stall;
					// Hardnekkig vast op dit segment (12s+): BUITEN ZICHT van de speler naar het
					// punt hoppen en verder lopen - de marker-lijn wordt zo altijd afgelegd, ook
					// waar navmesh en rechte lijn het allebei laten afweten (trapgat-randen).
					if (St.Stall >= 6)
					{
						bool bUnseen = true;
						for (FConstPlayerControllerIterator PIt = World->GetPlayerControllerIterator(); PIt; ++PIt)
						{
							const APlayerController* PCs = PIt->Get();
							const APawn* Pps = PCs ? PCs->GetPawn() : nullptr;
							if (!Pps) { continue; }
							const FVector To = Cur - Pps->GetActorLocation();
							if (To.Size() < 1200.f) { bUnseen = false; break; }
							if (To.Size() < 5000.f && FVector::DotProduct(PCs->GetControlRotation().Vector(), To.GetSafeNormal()) > 0.05f) { bUnseen = false; break; }
						}
						if (bUnseen)
						{
							Cw->SetActorLocation(Tgt + FVector(0.f, 0.f, 90.f), false, nullptr, ETeleportType::TeleportPhysics);
							++St.EntryIdx;
							St.Stall = 0;
							continue;
						}
					}
					if (AAIController* AI = Cast<AAIController>(Cw->GetController()))
					{
						// ALTIJD rechtstreeks van marker naar marker: pathfinding zocht anders
						// "slimme" kortere wegen buiten de gemarkeerde lijn om (dwars over het
						// zand het hotel uit). Jouw lijn is de wet; de hop-vangrail dekt klemmen.
						AI->MoveToLocation(Tgt, 60.f, true, false);
					}
					continue;
				}
				// CHILL-PLEK: erheen lopen en blijven staan tot de dag wisselt.
				if (!St.ChillSpot.IsNearlyZero())
				{
					int32 CurDay = -1;
					if (AWeedShopGameState* GSc = World->GetGameState<AWeedShopGameState>())
					{
						if (GSc->GetDayCycle()) { CurDay = GSc->GetDayCycle()->GetDayNumber(); }
					}
					if (St.ChillDay >= 0 && CurDay >= 0 && CurDay != St.ChillDay)
					{
						GChillTaken.Remove(ChillKey(St.ChillSpot));
						St.ChillSpot = FVector::ZeroVector; // nieuwe dag: weer de route op
					}
					else
					{
						if (FVector::Dist2D(Cur, St.ChillSpot) < 140.f) { St.Stall = 0; continue; } // staat te chillen
						if (Cw->GetVelocity().SizeSquared2D() > 25.f) { St.Stall = 0; continue; }
						++St.Stall;
						if (AAIController* AI = Cast<AAIController>(Cw->GetController()))
						{
							AI->MoveToLocation(St.ChillSpot, 80.f, true, !bDirectRing && St.Stall < 3);
						}
						continue;
					}
				}
				// Tijd om naar huis te gaan? Ketting omgekeerd teruglopen (zichtbaar het gebouw in).
				if (!St.bHomeward && St.ReturnPath.Num() >= 2 && World->GetRealTimeSeconds() > St.PatrolUntil && St.PatrolUntil > 0.f)
				{
					St.bHomeward = true;
					St.Entry = St.ReturnPath;
					St.EntryIdx = 0;
					St.Stall = 0;
					continue;
				}
				if (!NetAdj.IsValidIndex(St.NextIdx)) { St.NextIdx = 0; St.PrevIdx = -1; }
				const bool bArrived = FVector::Dist2D(Cur, NetNodes[St.NextIdx]) < 240.f;
				const bool bMoving = Cw->GetVelocity().SizeSquared2D() > 25.f;
				if (bArrived)
				{
					// Kruispunt: RECHTDOOR-voorkeur (75%) zodat ze niet constant oversteken of
					// zigzaggen - afslaan is de uitzondering. Nooit direct terug.
					const TArray<int32>& Nb = NetAdj[St.NextIdx];
					int32 Pick = St.NextIdx;
					if (Nb.Num() == 1) { Pick = Nb[0]; }
					else if (Nb.Num() > 1)
					{
						FVector InDir = FVector::ZeroVector;
						if (NetNodes.IsValidIndex(St.PrevIdx))
						{
							InDir = (NetNodes[St.NextIdx] - NetNodes[St.PrevIdx]).GetSafeNormal2D();
						}
						int32 Straight = -1;
						float BestDot = -2.f;
						for (int32 NbIdx : Nb)
						{
							if (NbIdx == St.PrevIdx) { continue; }
							const float Dot = InDir.IsNearlyZero() ? 0.f
								: FVector::DotProduct(InDir, (NetNodes[NbIdx] - NetNodes[St.NextIdx]).GetSafeNormal2D());
							if (Dot > BestDot) { BestDot = Dot; Straight = NbIdx; }
						}
						if (Straight >= 0 && BestDot > 0.5f && FMath::FRand() < 0.75f)
						{
							Pick = Straight; // gewoon doorlopen
						}
						else
						{
							for (int32 t = 0; t < 6; ++t)
							{
								const int32 Cand = Nb[FMath::RandRange(0, Nb.Num() - 1)];
								if (Cand != St.PrevIdx) { Pick = Cand; break; }
							}
							if (Pick == St.NextIdx) { Pick = (Straight >= 0) ? Straight : Nb[0]; }
						}
					}
					St.PrevIdx = St.NextIdx;
					St.NextIdx = Pick;
					St.StallRounds = 0;
				}
				if (!bArrived && bMoving) { St.Stall = 0; St.StallRounds = 0; continue; }
				if (!bArrived) { ++St.Stall; }
				// VASTLOPER-HERSTEL: na 6 mislukte tikken is het doel kennelijk onbereikbaar
				// (navmesh-gat) - dichtstbijzijnde knoop herberekenen en een ANDERE kant op.
				if (St.Stall >= 6)
				{
					float BD2 = TNumericLimits<float>::Max();
					int32 NearN = St.NextIdx;
					for (int32 ri = 0; ri < NetNodes.Num(); ++ri)
					{
						const float Dd = FVector::DistSquared2D(NetNodes[ri], Cur);
						if (Dd < BD2) { BD2 = Dd; NearN = ri; }
					}
					const TArray<int32>& NbR = NetAdj.IsValidIndex(NearN) ? NetAdj[NearN] : NetAdj[0];
					int32 NewPick = NearN;
					for (int32 t = 0; t < 6; ++t)
					{
						const int32 Cand = NbR.Num() > 0 ? NbR[FMath::RandRange(0, NbR.Num() - 1)] : NearN;
						if (Cand != St.NextIdx) { NewPick = Cand; break; }
					}
					// 2e herstel-ronde ZONDER vooruitgang: klem achter geometrie - typisch de hal/lobby na
					// het entry-pad, waar de directe ring-walk dwars door de muur naar buiten mikt en de
					// hal->route-overdracht dus nooit afkomt (de NPC stond daar stil tot de despawn). Zelfde
					// vangrail als op het entry-pad: BUITEN ZICHT van alle spelers naar de dichtstbijzijnde
					// knoop hoppen en gewoon verder patrouilleren - geen zichtbare pop.
					++St.StallRounds;
					if (St.StallRounds >= 2)
					{
						bool bUnseen = true;
						for (FConstPlayerControllerIterator PIt = World->GetPlayerControllerIterator(); PIt; ++PIt)
						{
							const APlayerController* PCs = PIt->Get();
							const APawn* Pps = PCs ? PCs->GetPawn() : nullptr;
							if (!Pps) { continue; }
							const FVector To = Cur - Pps->GetActorLocation();
							if (To.Size() < 1200.f) { bUnseen = false; break; }
							if (To.Size() < 5000.f && FVector::DotProduct(PCs->GetControlRotation().Vector(), To.GetSafeNormal()) > 0.05f) { bUnseen = false; break; }
						}
						if (bUnseen && NetNodes.IsValidIndex(NearN))
						{
							Cw->SetActorLocation(NetNodes[NearN] + FVector(0.f, 0.f, 90.f), false, nullptr, ETeleportType::TeleportPhysics);
							St.StallRounds = 0;
						}
					}
					St.PrevIdx = NearN;
					St.NextIdx = NewPick;
					St.Stall = 0;
				}
				if (AAIController* AI = Cast<AAIController>(Cw->GetController()))
				{
					const FVector Jit(FMath::FRandRange(-140.f, 140.f), FMath::FRandRange(-140.f, 140.f), 0.f);
					FVector Goal = NetNodes[St.NextIdx] + Jit;
					if (!bDirectRing)
					{
						// Navmesh leeft: de knoop netjes op de stoep projecteren (props ernaast vermijden).
						FNavLocation GoalNav;
						if (Nav->ProjectPointToNavigation(Goal, GoalNav, FVector(200.f, 200.f, ZTol))
							&& FMath::Abs(GoalNav.Location.Z - NetNodes[St.NextIdx].Z) <= ZTol
							&& IsOnStreetSurface(World, GoalNav.Location))
						{
							Goal = GoalNav.Location; // netjes op de stoep, niet op een container ernaast
						}
						else
						{
							Goal = NetNodes[St.NextIdx]; // jitter viel verkeerd: de kale knoop (= jouw lijn)
						}
					}
					else
					{
						// Dode navmesh: GEEN projectie (die faalt of mikt verkeerd) - mik op de kale knoop
						// (= jouw gemarkeerde ring, bewezen beloopbaar) en loop er RECHTSTREEKS heen.
						Goal = NetNodes[St.NextIdx];
					}
					// bUsePathfinding=false zodra de navmesh dood is (of na 3 stalls): CharacterMovement
					// loopt rechtstreeks naar Goal en botst nog steeds tegen muren/vloer (geen teleport).
					AI->MoveToLocation(Goal, 90.f, true, !bDirectRing && St.Stall < 3);
				}
			}
		}
		else
		{
			// Geen route: los rondslenteren rond dit punt (op stoep-hoogte).
			for (const TObjectPtr<ACustomerBase>& Cw : Spawned)
			{
				if (!IsValid(Cw) || Cw->bNeedsPlayer) { continue; }
				if (Cw->GetVelocity().SizeSquared2D() > 25.f) { continue; }
				if (FMath::FRand() > 0.6f) { continue; }
				FNavLocation Goal;
				const FVector GAround = GetActorLocation() + FVector(FMath::FRandRange(-SpotRadius * 2.f, SpotRadius * 2.f), FMath::FRandRange(-SpotRadius * 2.f, SpotRadius * 2.f), 0.f);
				if (!Nav->ProjectPointToNavigation(GAround, Goal, FVector(400.f, 400.f, ZTol))) { continue; }
				if (FMath::Abs(Goal.Location.Z - GetActorLocation().Z) > ZTol) { continue; }
				if (AAIController* AI = Cast<AAIController>(Cw->GetController()))
				{
					AI->MoveToLocation(Goal.Location, 60.f);
				}
			}
		}
		if (CountRoamers(Spawned) >= NightOrDayTarget) { return; } // dag = vol, nacht = klein (junkies)
		++TryCount;
		if (TryCount % 30 == 0)
		{
			UE_LOG(LogWeedShop, Verbose, TEXT("Spawner (%.0f, %.0f, %.0f): %d/%d - afgekeurd nav=%d hoogte=%d straat=%d zicht=%d"), // Verbose: pure spawn-rejectie-diagnose, vuurde elke 30 pogingen (552 regels/sessie) - alleen nodig bij stad-populatie-debug
				GetActorLocation().X, GetActorLocation().Y, GetActorLocation().Z, Spawned.Num(), MaxCustomers, RejNav, RejZ, RejStreet, RejView);
		}
		// DAG-START VULLING: bij sessiestart en elke nieuwe dag gooit de spawner zichzelf in
		// een klap vol (zonder de niet-in-beeld-regel) - de hele strip staat dan meteen vol
		// volk, boven en onder. Daarna geldt de normale druppel-aanvulling met alle regels.
		int32 BurstDay = -1;
		if (AWeedShopGameState* GSb = World->GetGameState<AWeedShopGameState>())
		{
			if (GSb->GetDayCycle()) { BurstDay = GSb->GetDayCycle()->GetDayNumber(); }
		}
		if (BurstDay >= 0 && BurstDay != LastBurstDay)
		{
			int32 Guard = MaxCustomers * 8;
			while (CountRoamers(Spawned) < MaxCustomers && Guard-- > 0)
			{
				FNavLocation BNav;
				const FVector BAround = GetActorLocation() + FVector(FMath::FRandRange(-SpotRadius, SpotRadius), FMath::FRandRange(-SpotRadius, SpotRadius), 0.f);
				if (!Nav->ProjectPointToNavigation(BAround, BNav, FVector(400.f, 400.f, ZTol))) { ++RejNav; continue; }
				if (FMath::Abs(BNav.Location.Z - GetActorLocation().Z) > ZTol) { ++RejZ; continue; }
				if (!IsOnStreetSurface(World, BNav.Location)) { ++RejStreet; continue; }
				// Ook de dag-burst spawnt NIET voor je neus: in beeld of te dichtbij -> sla deze plek over.
				{
					bool bBurstSeen = false;
					for (FConstPlayerControllerIterator PIt = World->GetPlayerControllerIterator(); PIt; ++PIt)
					{
						const APlayerController* PCp = PIt->Get();
						const APawn* Pp = PCp ? PCp->GetPawn() : nullptr;
						if (!Pp) { continue; }
						const FVector To = BNav.Location - Pp->GetActorLocation();
						const float Dp = To.Size2D();
						if (Dp < 2500.f) { bBurstSeen = true; break; }
						if (Dp < 6000.f && FVector::DotProduct(PCp->GetControlRotation().Vector().GetSafeNormal2D(), To.GetSafeNormal2D()) > 0.1f) { bBurstSeen = true; break; }
					}
					if (bBurstSeen) { ++RejView; continue; }
				}
				TSubclassOf<ACustomerBase> BCls = CustomerClass;
				if (!BCls) { BCls = ACustomerBase::StaticClass(); }
				// DEFERRED + bCrowdNpc VOOR BeginPlay: ambient walker -> goedkope 1-mesh-build (geen modulaire spawn-hitch).
				ACustomerBase* BC = World->SpawnActorDeferred<ACustomerBase>(BCls, FTransform(BNav.Location + FVector(0.f, 0.f, 100.f)), nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
				if (BC) { BC->bCrowdNpc = true; BC->FinishSpawning(FTransform(BNav.Location + FVector(0.f, 0.f, 100.f))); }
				if (BC)
				{
					LastBurstDay = BurstDay; // dag-beurt pas verbruikt zodra er echt iets lukt
					Spawned.Add(BC);
					MakeWalkerCheap(BC);
					if (UCharacterMovementComponent* BMv = BC->GetCharacterMovement()) { BMv->MaxWalkSpeed = 165.f; }
					if (NetNodes.Num() >= 2)
					{
						FPatrolState BSt;
						float BBD = TNumericLimits<float>::Max();
						for (int32 ri = 0; ri < NetNodes.Num(); ++ri)
						{
							const float Dd = FVector::DistSquared2D(NetNodes[ri], BC->GetActorLocation());
							if (Dd < BBD) { BBD = Dd; BSt.NextIdx = ri; }
						}
						BSt.PrevIdx = -1;
						Patrol.Add(BC, BSt);
					}
				}
			}
			return;
		}
		// SNELLER VULLEN: meerdere kandidaat-pogingen per tik - afkeuringen (geen straat onder
		// de voeten, verkeerde hoogte) verspillen dan geen hele beurt meer. De stad vult zo in
		// seconden in plaats van minuten, ook de overkant van de grote weg.
		FNavLocation SpawnNav;
		bool bFound = false;
		for (int32 Attempt = 0; Attempt < 6 && !bFound; ++Attempt)
		{
			const FVector Around = GetActorLocation() + FVector(FMath::FRandRange(-SpotRadius, SpotRadius), FMath::FRandRange(-SpotRadius, SpotRadius), 0.f);
			if (!Nav->ProjectPointToNavigation(Around, SpawnNav, FVector(400.f, 400.f, ZTol))) { ++RejNav; continue; }
			if (FMath::Abs(SpawnNav.Location.Z - GetActorLocation().Z) > ZTol) { ++RejZ; continue; } // onderniveau
			if (!IsOnStreetSurface(World, SpawnNav.Location)) { ++RejStreet; continue; } // alleen straat/stoep
			bFound = true;
		}
		if (!bFound) { return; }
		// NIET voor de neus van de speler verschijnen: ver weg (60m+) mag altijd, dichterbij
		// alleen als de plek BUITEN beeld ligt (achter de kijkrichting). Zo zie je ze nooit
		// poppen - alleen aan komen lopen.
		for (FConstPlayerControllerIterator PIt = World->GetPlayerControllerIterator(); PIt; ++PIt)
		{
			const APlayerController* PC = PIt->Get();
			const APawn* Pp = PC ? PC->GetPawn() : nullptr;
			if (!Pp) { continue; }
			const FVector To = SpawnNav.Location - Pp->GetActorLocation();
			const float Dp = To.Size2D();
			if (Dp < 2500.f) { ++RejView; return; } // te dichtbij: altijd merkbaar
			if (Dp < 6000.f)
			{
				const FVector Dir = To.GetSafeNormal2D();
				const FVector View = PC->GetControlRotation().Vector().GetSafeNormal2D();
				if (FVector::DotProduct(View, Dir) > 0.1f) { ++RejView; return; } // in beeld: volgende keer
			}
		}
		TSubclassOf<ACustomerBase> Cls = CustomerClass;
		if (!Cls) { Cls = ACustomerBase::StaticClass(); }
		// DEFERRED + bCrowdNpc VOOR BeginPlay: ambient walker -> goedkope 1-mesh-build (geen modulaire spawn-hitch).
			ACustomerBase* C = World->SpawnActorDeferred<ACustomerBase>(Cls, FTransform(SpawnNav.Location + FVector(0.f, 0.f, 100.f)), nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			if (C) { C->bCrowdNpc = true; C->FinishSpawning(FTransform(SpawnNav.Location + FVector(0.f, 0.f, 100.f))); }
		if (C)
		{
			Spawned.Add(C);
			// Rustige wandeltred + start-patrouille: dichtstbijzijnde graaf-knoop.
			MakeWalkerCheap(C);
			if (UCharacterMovementComponent* Mv = C->GetCharacterMovement()) { Mv->MaxWalkSpeed = 165.f; }
			if (NetNodes.Num() >= 2)
			{
				FPatrolState St;
				float BD = TNumericLimits<float>::Max();
				for (int32 ri = 0; ri < NetNodes.Num(); ++ri)
				{
					const float Dd = FVector::DistSquared2D(NetNodes[ri], C->GetActorLocation());
					if (Dd < BD) { BD = Dd; St.NextIdx = ri; }
				}
				St.PrevIdx = -1;
				Patrol.Add(C, St);
			}
		}
		return;
	}

	// LEGACY CityGenerator-bewonerspad VERWIJDERD: ACityGenerator draaide alleen op de oude
	// Map_Apartment, nooit op CityBeachStrip. Op de echte speelmap is de City altijd nullptr,
	// dus de hele bewoners-/fixtures-/rotatie-tak werd daar nooit uitgevoerd. Alleen het
	// bSpawnResidents=false-pad hierboven (losse wandelaars) is nog actief.
}

bool ACustomerSpawner::RequestParkVisit(ACustomerBase* C)
{
	if (!C) { return false; }
	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	// Tickets opruimen: houder weg, trip niet (meer) actief (na een start-gratie van 5s), of veiligheids-
	// verloop na 240s (vastgelopen trip mag de rij niet eeuwig blokkeren).
	ParkTickets.RemoveAll([&](const FParkTicket& T)
	{
		if (!T.Holder.IsValid()) { return true; }
		const float Held = Now - T.GrantTime;
		if (Held > 240.f) { return true; }
		if (Held > 5.f && !T.Holder->IsParkTripActive()) { return true; }
		return false;
	});
	ParkQueue.RemoveAll([](const TWeakObjectPtr<ACustomerBase>& Q) { return !Q.IsValid(); });

	for (const FParkTicket& T : ParkTickets) { if (T.Holder.Get() == C) { return true; } } // al jouw beurt
	if (!ParkQueue.Contains(C)) { ParkQueue.Add(C); } // achteraan aansluiten

	const int32 MaxTickets = 4; // max tegelijk op park-trip (lopen + blijven) - genoeg doorstroom, geen prop
	while (ParkTickets.Num() < MaxTickets && ParkQueue.Num() > 0)
	{
		TWeakObjectPtr<ACustomerBase> Next = ParkQueue[0];
		ParkQueue.RemoveAt(0);
		if (Next.IsValid()) { ParkTickets.Add({ Next, Now }); }
	}
	for (const FParkTicket& T : ParkTickets) { if (T.Holder.Get() == C) { return true; } }
	return false;
}

void ACustomerSpawner::FinishParkVisit(ACustomerBase* C)
{
	ParkTickets.RemoveAll([&](const FParkTicket& T) { return !T.Holder.IsValid() || T.Holder.Get() == C; });
	ParkQueue.RemoveAll([&](const TWeakObjectPtr<ACustomerBase>& Q) { return !Q.IsValid() || Q.Get() == C; });
}
