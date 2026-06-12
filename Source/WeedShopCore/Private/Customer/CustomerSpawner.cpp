#include "Customer/CustomerSpawner.h"
#include "AIController.h"

#include "WeedShopCore.h"
#include "Customer/CustomerBase.h"
#include "World/CityGenerator.h"
#include "World/DayCycleComponent.h"
#include "World/StoreCounter.h"
#include "World/CityDoor.h"
#include "Game/WeedShopGameState.h"
#include "Npc/NpcRegistryComponent.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerStart.h"
#include "Placement/PlaceableProp.h"
#include "Placement/FurnitureTemplateLib.h"
#include "World/Atm.h"
#include "World/WaterSink.h"
#include "Save/SaveGameSubsystem.h"
#include "Engine/GameInstance.h"
#include "CollisionQueryParams.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "NavigationSystem.h"

ACustomerSpawner::ACustomerSpawner()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bTickEvenWhenPaused = true;
	// ZONDER root-component heeft een runtime-gespawnde spawner GEEN transform: GetActorLocation()
	// is dan altijd (0,0,0) - alle klanten spawnden daardoor op de oorsprong onder het dek.
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

void ACustomerSpawner::AdoptWalker(ACustomerBase* C)
{
	if (!C) { return; }
	Spawned.Add(C);
	if (UCharacterMovementComponent* Mv = C->GetCharacterMovement()) { Mv->MaxWalkSpeed = 165.f; }
	if (PatrolRoute.Num() >= 2)
	{
		FPatrolState St;
		float BD = TNumericLimits<float>::Max();
		for (int32 ri = 0; ri < PatrolRoute.Num(); ++ri)
		{
			const float Dd = FVector::DistSquared2D(PatrolRoute[ri], C->GetActorLocation());
			if (Dd < BD) { BD = Dd; St.NextIdx = ri; }
		}
		St.Dir = FMath::RandBool() ? 1 : -1;
		Patrol.Add(C, St);
	}
}

void ACustomerSpawner::BeginPlay()
{
	Super::BeginPlay();
	if (UWorld* World = GetWorld())
	{
		NextResidentSpawnTryRealTime = World->GetRealTimeSeconds() + 0.5f;
	}
	if (HasAuthority())
	{
		// Snel retry-interval zodat de bewoners verschijnen zodra de stad gebouwd is.
		GetWorldTimerManager().SetTimer(SpawnTimer, this, &ACustomerSpawner::TrySpawn, 2.0f, true, 1.0f);
	}
}

void ACustomerSpawner::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UWorld* World = GetWorld();
	if (!World || !HasAuthority())
	{
		return;
	}
	if (bResidentsSpawned)
	{
		GetWorldTimerManager().ClearTimer(SpawnTimer);
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

	ACityGenerator* City = nullptr;
	for (TActorIterator<ACityGenerator> It(World); It; ++It)
	{
		City = *It;
		break;
	}
	const FVector CityCenter = City ? City->GetCityCenter() : FVector::ZeroVector;
	const float CellSize = City ? FMath::Max(100.f, City->GetPitch()) : 3000.f;

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

void ACustomerSpawner::TrySpawn()
{
	UWorld* World = GetWorld();
	if (!World || !HasAuthority()) { return; }

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
		// STRAKKE hoogte-marge (50cm): het service-niveau (~200cm lager) EN terras-tafels
		// (~75cm hoger - navmesh-eilandjes waar NPC's op vast stonden) vallen er beide buiten.
		const float ZTol = 50.f;
		// Opruimen: wandelaars die GEZAKT zijn (onder de stoep) of ergens OP geklommen staan.
		// Hoogte vergelijken met het DICHTSTBIJZIJNDE route-punt (de route heeft verloop - tegen
		// de eigen spawner meten doodde wandelaars die gewoon de ring afliepen). Bewoners
		// (Resident_-id) zijn vrijgesteld: die zijn legitiem binnen/boven (trap, verdiepingen).
		for (int32 wi = Spawned.Num() - 1; wi >= 0; --wi)
		{
			ACustomerBase* Cw0 = Spawned[wi];
			if (!IsValid(Cw0)) { continue; }
			if (Cw0->NpcId.ToString().StartsWith(TEXT("Resident_"))) { continue; }
			const FVector L0 = Cw0->GetActorLocation();
			float RefZ = GetActorLocation().Z;
			if (PatrolRoute.Num() >= 2)
			{
				float BD0 = TNumericLimits<float>::Max();
				for (const FVector& RPt : PatrolRoute)
				{
					const float Dd0 = FVector::DistSquared2D(RPt, L0);
					if (Dd0 < BD0) { BD0 = Dd0; RefZ = RPt.Z; }
				}
			}
			const float Dz = L0.Z - RefZ;
			if (Dz < -150.f || Dz > 220.f)
			{
				Cw0->Destroy();
				Spawned.RemoveAt(wi);
			}
		}
		// ROUTE-PATROUILLE: wandelaars lopen de gemarkeerde ring punt-voor-punt af. Wie bij z'n
		// punt is (of stilstaat) krijgt het volgende punt, met wat zijwaartse variatie zodat het
		// geen ganzenmars wordt. Klanten die op de speler wachten (deal) blijven met rust.
		if (PatrolRoute.Num() >= 2)
		{
			for (const TObjectPtr<ACustomerBase>& Cw : Spawned)
			{
				if (!IsValid(Cw) || Cw->bNeedsPlayer) { continue; }
				FPatrolState& St = Patrol.FindOrAdd(Cw);
				const FVector Cur = Cw->GetActorLocation();
				const bool bArrived = FVector::Dist2D(Cur, PatrolRoute[St.NextIdx]) < 240.f;
				const bool bMoving = Cw->GetVelocity().SizeSquared2D() > 25.f;
				if (bArrived)
				{
					St.NextIdx = (St.NextIdx + St.Dir + PatrolRoute.Num()) % PatrolRoute.Num();
				}
				if (!bArrived && bMoving) { continue; }
				if (AAIController* AI = Cast<AAIController>(Cw->GetController()))
				{
					const FVector Jit(FMath::FRandRange(-140.f, 140.f), FMath::FRandRange(-140.f, 140.f), 0.f);
					FVector Goal = PatrolRoute[St.NextIdx] + Jit;
					FNavLocation GoalNav;
					if (Nav->ProjectPointToNavigation(Goal, GoalNav, FVector(200.f, 200.f, ZTol))
						&& FMath::Abs(GoalNav.Location.Z - PatrolRoute[St.NextIdx].Z) <= ZTol)
					{
						Goal = GoalNav.Location; // netjes op de stoep, niet op een tafel ernaast
					}
					else
					{
						Goal = PatrolRoute[St.NextIdx]; // jitter viel verkeerd: het kale route-punt
					}
					AI->MoveToLocation(Goal, 90.f);
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
		if (Spawned.Num() >= MaxCustomers) { return; }
		FNavLocation SpawnNav;
		const FVector Around = GetActorLocation() + FVector(FMath::FRandRange(-SpotRadius, SpotRadius), FMath::FRandRange(-SpotRadius, SpotRadius), 0.f);
		if (!Nav->ProjectPointToNavigation(Around, SpawnNav, FVector(400.f, 400.f, ZTol))) { return; }
		if (FMath::Abs(SpawnNav.Location.Z - GetActorLocation().Z) > ZTol) { return; } // onderniveau geweigerd
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
			if (Dp < 2500.f) { return; } // te dichtbij: altijd merkbaar
			if (Dp < 6000.f)
			{
				const FVector Dir = To.GetSafeNormal2D();
				const FVector View = PC->GetControlRotation().Vector().GetSafeNormal2D();
				if (FVector::DotProduct(View, Dir) > 0.1f) { return; } // in beeld: volgende keer
			}
		}
		FActorSpawnParameters SP;
		SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		TSubclassOf<ACustomerBase> Cls = CustomerClass;
		if (!Cls) { Cls = ACustomerBase::StaticClass(); }
		ACustomerBase* C = World->SpawnActor<ACustomerBase>(Cls, FTransform(SpawnNav.Location + FVector(0.f, 0.f, 100.f)), SP);
		if (C)
		{
			Spawned.Add(C);
			// Rustige wandeltred + start-patrouille: dichtstbijzijnde route-punt, willekeurige kant op.
			if (UCharacterMovementComponent* Mv = C->GetCharacterMovement()) { Mv->MaxWalkSpeed = 165.f; }
			if (PatrolRoute.Num() >= 2)
			{
				FPatrolState St;
				float BD = TNumericLimits<float>::Max();
				for (int32 ri = 0; ri < PatrolRoute.Num(); ++ri)
				{
					const float Dd = FVector::DistSquared2D(PatrolRoute[ri], C->GetActorLocation());
					if (Dd < BD) { BD = Dd; St.NextIdx = ri; }
				}
				St.Dir = FMath::RandBool() ? 1 : -1;
				Patrol.Add(C, St);
			}
		}
		return;
	}

	// De stad-bevolking = bewoners (dealbaar). Eén keer toewijzen zodra de stad gebouwd is.
	// (Geen test/random-modus meer: die ruimde alle klanten op behalve één.)
	if (!bResidentsSpawned) { SpawnResidents(); }
	if (bResidentsSpawned)
	{
		GetWorldTimerManager().ClearTimer(SpawnTimer);
		StartResidentMovementMonitor();
	}
}

namespace
{
	// Unieke korte bewonersnaam per woning. Op INDEX (niet hash) -> elke woning een eigen
	// voor+achternaam zonder botsingen, met namen die netjes in deur/phone UI passen.
	FString ResidentNameByIndex(int32 Index)
	{
		static const TCHAR* First[] = {
			TEXT("Jan"), TEXT("Piet"), TEXT("Kees"), TEXT("Henk"), TEXT("Cor"), TEXT("Sjonnie"), TEXT("Henkie"), TEXT("Appie"),
			TEXT("Bertus"), TEXT("Guus"), TEXT("Klaas"), TEXT("Mees"), TEXT("Sjakie"), TEXT("Dirk"), TEXT("Wim"), TEXT("Bram"),
			TEXT("Joost"), TEXT("Sven"), TEXT("Tim"), TEXT("Rick"), TEXT("Bas"), TEXT("Daan"), TEXT("Niels"), TEXT("Koen"),
			TEXT("Gijs"), TEXT("Teun"), TEXT("Luuk"), TEXT("Stijn"), TEXT("Jasper"), TEXT("Ruben"), TEXT("Lars"), TEXT("Mats"),
			TEXT("Cas"), TEXT("Sander"), TEXT("Bart"), TEXT("Wout"), TEXT("Tijn"), TEXT("Siem"), TEXT("Boaz"), TEXT("Jules"),
			TEXT("Sam"), TEXT("Mick"), TEXT("Thijs"), TEXT("Ravi"), TEXT("Roel"), TEXT("Maarten"), TEXT("Freek"), TEXT("Jelle"),
			TEXT("Floris"), TEXT("Hugo"), TEXT("Pim"), TEXT("Joris"), TEXT("Tom"), TEXT("Wessel"), TEXT("Lucas"), TEXT("Milan"),
			TEXT("Finn"), TEXT("Noud"), TEXT("Sanne"), TEXT("Emma"), TEXT("Lotte"), TEXT("Fleur"), TEXT("Iris"), TEXT("Roos"),
			TEXT("Femke"), TEXT("Tessa"), TEXT("Maud"), TEXT("Nina"), TEXT("Lieke"), TEXT("Nora") };
		static const TCHAR* Last[] = {
			TEXT("Vapehoven"), TEXT("Kushman"), TEXT("Hashberg"), TEXT("Bongers"), TEXT("Highsma"), TEXT("Wietveld"), TEXT("Blunt"), TEXT("Stoner"),
			TEXT("Greenwood"), TEXT("Hazelaar"), TEXT("Spliffstra"), TEXT("Dankzaad"), TEXT("Nugteren"), TEXT("Pofadder"), TEXT("Knaller"), TEXT("Patatje"),
			TEXT("Frikandel"), TEXT("Stamppot"), TEXT("Bitterbal"), TEXT("Kroketberg"), TEXT("Pindakaas"), TEXT("Hagelslag"), TEXT("Stroopwafel"), TEXT("Kaaskop"),
			TEXT("Klompenburg"), TEXT("Tulpman"), TEXT("Windmolen"), TEXT("Fietsbel"), TEXT("Gouda"), TEXT("Poffertje"), TEXT("Bamischijf"), TEXT("Kapsalon"),
			TEXT("Snackbar"), TEXT("Wietstra"), TEXT("Blowman"), TEXT("Puffinga"), TEXT("Dampkring"), TEXT("Rookwolk"), TEXT("Jointsma"), TEXT("Dabberhof"),
			TEXT("Bongerd"), TEXT("Wietema"), TEXT("Hennep"), TEXT("Grasveld"), TEXT("Blowinga"), TEXT("Smoorman"), TEXT("Peukstra"), TEXT("Asbakker"),
			TEXT("Vloeitje"), TEXT("Aansteker"), TEXT("Hasjman"), TEXT("Wiedman"), TEXT("Coffeeshop"), TEXT("Theehuis"), TEXT("Spacecake"), TEXT("Brownie"),
			TEXT("Koekenbakker"), TEXT("Pannekoek"), TEXT("Oliebol"), TEXT("Kroket"), TEXT("Kibbeling"), TEXT("Lekkerbek"), TEXT("Haringman"), TEXT("Mosselman"),
			TEXT("Patatkraam"), TEXT("Mayoman"), TEXT("Currysaus"), TEXT("Berenburg"), TEXT("Jenever"), TEXT("Klompmaker"), TEXT("Polderman"), TEXT("Dijkgraaf"),
			TEXT("Grachtgordel"), TEXT("Fietspad"), TEXT("Marktplein"), TEXT("Tulpenveld"), TEXT("Molenaar"), TEXT("Kaasboer"), TEXT("Melkboer"), TEXT("Groenteman"),
			TEXT("Slagerman"), TEXT("Bakkerman"), TEXT("Sjekkie"), TEXT("Shagman"), TEXT("Grinder"), TEXT("Bongwater"), TEXT("Waterpijp"), TEXT("Stickie"),
			TEXT("Dampman"), TEXT("Rookgordel"), TEXT("Blowveld"), TEXT("Hasjbrik"), TEXT("Wietpot"), TEXT("Hasjpijp"), TEXT("Nederwiet"), TEXT("Skunkstra"),
			TEXT("Paddoman"), TEXT("Truffel"), TEXT("Jointman"), TEXT("Vuurtje") };
		const int32 NF = (int32)UE_ARRAY_COUNT(First);
		const int32 NL = (int32)UE_ARRAY_COUNT(Last);
		const int32 I = FMath::Max(0, Index);
		return FString::Printf(TEXT("%s %s"), First[I % NF], Last[(I * 37 + I / NF) % NL]);
	}

	bool AreResidentEdgeHomesNavigationReady(UWorld* World, const TArray<FApartmentHome>& Homes, const TSet<int32>& ForSale)
	{
		UNavigationSystemV1* Nav = World ? UNavigationSystemV1::GetCurrent(World) : nullptr;
		if (!Nav || Homes.Num() == 0)
		{
			return false;
		}

		const FVector2D Dirs[] = {
			FVector2D(1.f, 0.f), FVector2D(-1.f, 0.f), FVector2D(0.f, 1.f), FVector2D(0.f, -1.f),
			FVector2D(1.f, 1.f), FVector2D(1.f, -1.f), FVector2D(-1.f, 1.f), FVector2D(-1.f, -1.f)
		};

		TArray<int32> SampleHomes;
		for (const FVector2D& RawDir : Dirs)
		{
			const FVector2D Dir = RawDir.GetSafeNormal();
			int32 BestIndex = INDEX_NONE;
			float BestScore = -TNumericLimits<float>::Max();
			for (int32 i = 0; i < Homes.Num(); ++i)
			{
				if (ForSale.Contains(i))
				{
					continue;
				}

				const FVector& Door = Homes[i].DoorPos;
				const float Score = Door.X * Dir.X + Door.Y * Dir.Y;
				if (Score > BestScore)
				{
					BestScore = Score;
					BestIndex = i;
				}
			}
			if (BestIndex != INDEX_NONE)
			{
				SampleHomes.AddUnique(BestIndex);
			}
		}

		if (SampleHomes.Num() == 0)
		{
			return false;
		}

		const FVector Extent(800.f, 800.f, 1200.f);
		for (int32 HomeIndex : SampleHomes)
		{
			FNavLocation Projected;
			if (!Nav->ProjectPointToNavigation(Homes[HomeIndex].DoorPos, Projected, Extent))
			{
				return false;
			}
		}
		return true;
	}
}

void ACustomerSpawner::SpawnResidents()
{
	UWorld* World = GetWorld();
	if (!World || !HasAuthority()) { return; }

	// Eén spawner regelt de bewoners: staan er al bewoners, dan niets doen (geen dubbele).
	for (TActorIterator<ACustomerBase> It(World); It; ++It)
	{
		if (IsValid(*It) && It->IsResident()) { bResidentsSpawned = true; return; }
	}

	// Vind de (lokaal gebouwde) stad met geregistreerde woningen.
	ACityGenerator* City = nullptr;
	for (TActorIterator<ACityGenerator> It(World); It; ++It) { City = *It; break; }
	if (!City) { return; }
	const TArray<FApartmentHome>& Homes = City->GetApartmentHomes();
	if (Homes.Num() == 0) { return; } // stad nog niet klaar -> volgende tick

	// ELKE genummerde woning krijgt een bewoner (deur op slot met naam). Een gespreide subset loopt
	// ook ECHT rond (MaxResidents; 0 = allemaal). De rest "woont er" via de op-slot-deur met naam.
	const int32 Total = Homes.Num();

	// De 3 koopbare panden (starter-flatje, rijtjeshuis, grote kamer) blijven ALTIJD vrij: geen bewoner,
	// deur als "FOR SALE" totdat de speler 'm koopt (dan ontgrendelt de PhoneClientComponent 'm lokaal).
	TArray<FCityPropertyOffer> Offers; City->GetPropertyOffers(Offers);
	TSet<int32> ForSale;
	for (const FCityPropertyOffer& O : Offers) { for (int32 Idx : O.Homes) { ForSale.Add(Idx); } }

	if (!AreResidentEdgeHomesNavigationReady(World, Homes, ForSale))
	{
		return;
	}

	bResidentsSpawned = true;

	// Ruim eerst alle vooraf-geplaatste/los rondslingerende klant-NPC's op (bv. test-NPC's die in de
	// level-map midden in de stad staan). Op dit punt bestaan er nog geen bewoners, dus dit raakt alleen
	// die ongewenste losse NPC's. Daarna spawnen we de echte bewoners op de woningen.
	for (TActorIterator<ACustomerBase> It(World); It; ++It)
	{
		if (IsValid(*It)) { It->Destroy(); }
	}

	// Op een VERSE game ook de editor-geplaatste meubel-props uit de level-map opruimen (bv. een test
	// tafel/koelkast/matras/gootsteen-set midden in de stad/park). Op dit punt bestaan er nog geen
	// procedurele fixtures of speler-geplaatste props, dus dit raakt alleen die level-rommel. Bij een
	// GELADEN game niet: dan herstelt de save de speler-props.
	bool bFreshForCleanup = true;
	if (UGameInstance* GI = World->GetGameInstance())
	{
		if (USaveGameSubsystem* Sv = GI->GetSubsystem<USaveGameSubsystem>()) { bFreshForCleanup = Sv->IsFreshGame(); }
	}
	if (bFreshForCleanup)
	{
		for (TActorIterator<APlaceableProp> It(World); It; ++It) { if (IsValid(*It)) { It->Destroy(); } }
		for (TActorIterator<AWaterSink> It(World); It; ++It) { if (IsValid(*It)) { It->Destroy(); } }
	}

	AWeedShopGameState* GS = World->GetGameState<AWeedShopGameState>();
	UNpcRegistryComponent* Reg = GS ? GS->GetNpcRegistry() : nullptr;

	TSubclassOf<ACustomerBase> Cls = CustomerClass;
	if (!Cls) { Cls = ACustomerBase::StaticClass(); }

	// Fysieke roamers kiezen we per UNIEKE hoofdingang: alle units van één appartement-/flatgebouw delen
	// dezelfde DoorPos, dus zonder dit zouden er tig bewoners op exact dezelfde stoephoek clusteren.
	// Eén roamer per ingang, gespreid over de stad.
	auto DoorKey = [](const FVector& P) { return FIntVector(FMath::RoundToInt(P.X / 50.f), FMath::RoundToInt(P.Y / 50.f), 0); };
	TArray<int32> AllUnits;  // ALLE niet-koopbare units = rotatie-pool: zo komt over de dagen iedereen met een deur buiten
	TArray<int32> Entrances; // initiele actieve pool: rijtjeshuis = 1, flatgebouw = paar units VERSPREID over de verdiepingen
	{
		TMap<FIntVector, TArray<int32>> ByEntrance;
		TArray<FIntVector> Order;
		for (int32 i = 0; i < Total; ++i)
		{
			if (ForSale.Contains(i)) { continue; }
			AllUnits.Add(i);
			const FIntVector Key = DoorKey(Homes[i].DoorPos);
			TArray<int32>& Arr = ByEntrance.FindOrAdd(Key);
			if (Arr.Num() == 0) { Order.Add(Key); }
			Arr.Add(i);
		}
		for (const FIntVector& Key : Order)
		{
			TArray<int32> Units = ByEntrance[Key];
			if (Units.Num() <= 1) { Entrances.Add(Units[0]); continue; } // rijtjeshuis: 1 unit
			// Flatgebouw: pak ~3 units VERSPREID over de verdiepingen (begane grond, midden, boven) in die volgorde,
			// zodat de geografische round-robin per ronde een andere verdieping activeert -> je ziet ook bewoners
			// van bovenverdiepingen naar beneden komen. De rest komt via de dagelijkse rotatie over de tijd buiten.
			Units.Sort([&Homes](int32 A, int32 B) { return Homes[A].Floor < Homes[B].Floor; });
			const int32 Take = FMath::Min(3, Units.Num());
			for (int32 t = 0; t < Take; ++t)
			{
				const int32 Sel = (Take <= 1) ? 0 : (t * (Units.Num() - 1)) / (Take - 1);
				Entrances.AddUnique(Units[Sel]);
			}
		}
	}
	const bool bAll = (MaxResidents <= 0);
	const int32 UWant = bAll ? Entrances.Num() : FMath::Clamp(MaxResidents, 0, Entrances.Num());

	// GEOGRAFISCH spreiden: groepeer ingangen per bouwblok en kies round-robin 1 roamer per blok (dan
	// een 2e ronde, enz.). Anders komen de roamers vooral uit de centrale blokken en blijven ze in het
	// midden hangen.
	const float Cell = FMath::Max(100.f, City->GetPitch());
	TMap<FIntPoint, TArray<int32>> Cells;
	TArray<FIntPoint> CellOrder;
	for (int32 idx : Entrances)
	{
		const FVector& P = Homes[idx].DoorPos;
		const FIntPoint Key(FMath::RoundToInt(P.X / Cell), FMath::RoundToInt(P.Y / Cell));
		TArray<int32>& Arr = Cells.FindOrAdd(Key);
		if (Arr.Num() == 0) { CellOrder.Add(Key); }
		Arr.Add(idx);
	}
	TSet<int32> PhysicalSet;
	for (int32 round = 0, made = 0; made < UWant; ++round)
	{
		TArray<FIntPoint> RoundKeys;
		RoundKeys.Reserve(CellOrder.Num());
		for (const FIntPoint& Key : CellOrder)
		{
			const TArray<int32>& Arr = Cells[Key];
			if (round < Arr.Num()) { RoundKeys.Add(Key); }
		}

		if (RoundKeys.Num() == 0) { break; } // alle cellen leeg -> klaar

		const int32 Remaining = UWant - made;
		if (Remaining >= RoundKeys.Num())
		{
			for (const FIntPoint& Key : RoundKeys)
			{
				const TArray<int32>& Arr = Cells[Key];
				PhysicalSet.Add(Arr[round]);
				++made;
			}
			continue;
		}

		// Minder fysieke bewoners dan bouwblokken: pak een gelijkmatig verdeelde subset over de HELE stad
		// i.p.v. de eerste N generator-cellen, anders blijven late randblokken zonder echte roamer.
		for (int32 Pick = 0; Pick < Remaining; ++Pick)
		{
			const int32 KeyIndex = FMath::Clamp(
				FMath::FloorToInt((static_cast<float>(Pick) + 0.5f) * static_cast<float>(RoundKeys.Num()) / static_cast<float>(Remaining)),
				0,
				RoundKeys.Num() - 1);
			const TArray<int32>& Arr = Cells[RoundKeys[KeyIndex]];
			PhysicalSet.Add(Arr[round]);
			++made;
		}
	}

	// Garandeer dat er onder de roamers die naar buiten komen genoeg KOPERS zitten (addiction >= 30),
	// zodat je nooit toevallig alleen lage-addiction-mensen op straat hebt. Vervang zo nodig een paar
	// niet-kopers door koper-bewoners uit de rest van de stad (stats blijven 1x-deterministisch).
	{
		auto HomeAddiction = [](int32 HomeIdx) -> float
		{
			float R = 0.f, L = 0.f, A = 0.f;
			UNpcRegistryComponent::PredictPersonality(FName(*FString::Printf(TEXT("Resident_%04d"), HomeIdx)), R, L, A);
			return A;
		};
		const int32 BuyTarget = FMath::Min(UWant, FMath::Max(3, (UWant * 15) / 100)); // overdag bescheiden; de nacht regelt de verslaafden-surge
		int32 BuyersIn = 0;
		for (int32 Idx : PhysicalSet) { if (HomeAddiction(Idx) >= 30.f) { ++BuyersIn; } }
		if (BuyersIn < BuyTarget)
		{
			TArray<int32> BuyerPool;  // koper-ingangen die nog NIET gekozen zijn
			for (int32 Idx : Entrances) { if (!PhysicalSet.Contains(Idx) && HomeAddiction(Idx) >= 30.f) { BuyerPool.Add(Idx); } }
			TArray<int32> NonBuyersIn; // gekozen niet-kopers die we mogen vervangen
			for (int32 Idx : PhysicalSet) { if (HomeAddiction(Idx) < 30.f) { NonBuyersIn.Add(Idx); } }
			int32 bi = 0;
			while (BuyersIn < BuyTarget && bi < BuyerPool.Num() && NonBuyersIn.Num() > 0)
			{
				PhysicalSet.Remove(NonBuyersIn.Pop());
				PhysicalSet.Add(BuyerPool[bi++]);
				++BuyersIn;
			}
		}
	}

	int32 Made = 0;
	for (int32 i = 0; i < Total; ++i)
	{
		const FApartmentHome& H = Homes[i];
		if (ForSale.Contains(i))
		{
			if (ACityDoor* Dr = H.Door.Get()) { Dr->SetForSale(); }
			continue; // geen NPC in een koopbaar pand
		}
		const bool bPhysical = PhysicalSet.Contains(i);
		const FString DoorName = ResidentNameByIndex(i);

		if (bPhysical)
		{
			const FName ResidentNpcId(*FString::Printf(TEXT("Resident_%04d"), i));
			if (Reg)
			{
				Reg->EnsureNpc(ResidentNpcId, FText::FromString(DoorName));
			}

			const FTransform SpawnTM(FRotator::ZeroRotator, H.InteriorPos + FVector(0.f, 0.f, 4.f));
			if (ACustomerBase* C = World->SpawnActorDeferred<ACustomerBase>(
				Cls, SpawnTM, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn))
			{
				C->NpcId = Reg ? ResidentNpcId : NAME_None;
				C->FinishSpawning(SpawnTM);
				C->SetupResident(H.DoorPos, H.InteriorPos, H.Number, H.HallPos);
				if (Made == 0) // eerste bewoner = gegarandeerde koopklare test-klant
				{
					C->Respect = 70.f; C->Loyalty = 40.f; C->Addiction = 80.f;
					C->BecomeBuyerNow();
				}
				if (!C->GetController()) { C->SpawnDefaultController(); }
				Spawned.Add(C);
				++Made;
			}
		}
		// Deur op slot met een UNIEKE per-huisnummer naam (de registry round-robin kon namen herhalen,
		// waardoor meerdere huizen "dezelfde bewoner" leken te hebben). Elk huisnummer -> eigen naam.
		if (ACityDoor* Dr = H.Door.Get()) { Dr->SetResident(DoorName); }
	}

	if (Made < UWant)
	{
		UE_LOG(LogWeedShop, Warning, TEXT("Resident physical spawn shortfall: made=%d desired=%d homes=%d entrances=%d selected=%d forSale=%d"),
			Made, UWant, Total, Entrances.Num(), PhysicalSet.Num(), ForSale.Num());
	}
	else
	{
		UE_LOG(LogWeedShop, Log, TEXT("Resident physical spawn complete: made=%d desired=%d homes=%d entrances=%d selected=%d forSale=%d"),
			Made, UWant, Total, Entrances.Num(), PhysicalSet.Num(), ForSale.Num());
	}

	// Bij een verse game: meubels in de woningen + ATM in elke winkel (server-side, repliceert).
	ResidentHomeIndices = PhysicalSet; // bewoner-woningen meubileren we ook
	SpawnHomeAndShopFixtures(City);

	// Verkoper-NPC achter elke winkel-balie (server-side -> repliceert, geen duplicaten op clients).
	{
		TSubclassOf<ACustomerBase> KCls = CustomerClass; if (!KCls) { KCls = ACustomerBase::StaticClass(); }
		for (TActorIterator<AStoreCounter> It(World); It; ++It)
		{
			AStoreCounter* Counter = *It;
			if (!Counter || !Counter->HasShop()) { continue; }
			const FVector Fwd = Counter->GetActorForwardVector();
			// Character spawnt op CAPSULE-CENTRUM-hoogte (voeten op de vloer), niet op vloer-niveau - anders
			// zit de capsule half in de vloer en duwt de collision-adjust 'm naar een willekeurige plek
			// (winkelier stond daardoor nergens/ergens vast i.p.v. achter de balie).
			float HalfH = 88.f;
			if (const ACustomerBase* CDO = KCls->GetDefaultObject<ACustomerBase>())
			{
				if (const UCapsuleComponent* Cap = CDO->GetCapsuleComponent()) { HalfH = Cap->GetScaledCapsuleHalfHeight(); }
			}
			FVector Pos = Counter->GetActorLocation() - Fwd * 80.f;            // achter de balie
			Pos.Z = Counter->GetActorLocation().Z - 10.f + HalfH + 2.f;        // vloer + halve capsule
			const FTransform KTM(Counter->GetActorRotation(), Pos);
			if (ACustomerBase* Keeper = World->SpawnActorDeferred<ACustomerBase>(KCls, KTM, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn))
			{
				Keeper->bShopkeeper = true;
				Keeper->FinishSpawning(KTM);
				if (!Keeper->GetController()) { Keeper->SpawnDefaultController(); }
			}
		}
	}

	// --- Dagelijkse rotatie opzetten ---
	EligibleHomes = AllUnits;             // rotatie-pool = ALLE niet-koopbare units (elke deur komt over de dagen buiten)
	PhysicalHomes = PhysicalSet;          // wie nu fysiek is
	ActivatedEverHomes = PhysicalSet;     // al getoond
	if (const AWeedShopGameState* GSr = World->GetGameState<AWeedShopGameState>())
	{
		if (const UDayCycleComponent* DC = GSr->GetDayCycle()) { LastRotationDay = DC->GetDayNumber(); }
	}
	GetWorldTimerManager().SetTimer(RotationTimer, this, &ACustomerSpawner::CheckResidentRotation, 8.f, true);
}

void ACustomerSpawner::CheckResidentRotation()
{
	UWorld* World = GetWorld();
	if (!World || !HasAuthority()) { return; }
	const AWeedShopGameState* GS = World->GetGameState<AWeedShopGameState>();
	const UDayCycleComponent* DC = GS ? GS->GetDayCycle() : nullptr;
	if (!DC) { return; }

	// Dag<->nacht-overgang: pas de straat-populatie aan (nacht = kleine, (bijna) volledig verslaafde crowd).
	const int8 NightNow = DC->IsNight() ? 1 : 0;
	if (NightNow != LastNightState)
	{
		LastNightState = NightNow;
		ApplyDayNightPopulation(NightNow == 1);
	}

	// Geleidelijke dag-bijvulling: spawn de resterende bewoners NIET in één frame (dat gaf een burst -> ze
	// emergen tegelijk en gaan samen tollen), maar een paar per ~0.5s tot MaxResidents. Zo verschijnen ze
	// gespreid na de dag/nacht-switch.
	if (NightNow == 0)
	{
		const int32 DayTarget = (MaxResidents > 0) ? MaxResidents : EligibleHomes.Num();
		if (PhysicalHomes.Num() < DayTarget && World->GetTimeSeconds() >= NextDayRefillTime)
		{
			ACityGenerator* City = nullptr;
			for (TActorIterator<ACityGenerator> It(World); It; ++It) { City = *It; break; }
			if (City)
			{
				TArray<int32> ActNew, ActOld;
				for (int32 Home : EligibleHomes)
				{
					if (PhysicalHomes.Contains(Home)) { continue; }
					if (ActivatedEverHomes.Contains(Home)) { ActOld.Add(Home); } else { ActNew.Add(Home); }
				}
				int32 Batch = 0;
				while (PhysicalHomes.Num() < DayTarget && Batch < 3 && (ActNew.Num() + ActOld.Num()) > 0)
				{
					TArray<int32>& Pool = (ActNew.Num() > 0) ? ActNew : ActOld;
					const int32 Idx = FMath::RandRange(0, Pool.Num() - 1);
					const int32 Home = Pool[Idx]; Pool.RemoveAtSwap(Idx);
					SpawnOneResident(City, Home, false);
					++Batch;
				}
				ResidentHomeIndices = PhysicalHomes;
				NextDayRefillTime = World->GetTimeSeconds() + 0.5f;
			}
		}
	}

	// Dagelijkse rotatie (nieuwe gezichten).
	if (RotatePerDay > 0)
	{
		const int32 Day = DC->GetDayNumber();
		if (Day != LastRotationDay) { LastRotationDay = Day; RotateResidents(); }
	}

}

void ACustomerSpawner::ApplyDayNightPopulation(bool bNight)
{
	UWorld* World = GetWorld();
	if (!World || !HasAuthority()) { return; }
	ACityGenerator* City = nullptr;
	for (TActorIterator<ACityGenerator> It(World); It; ++It) { City = *It; break; }
	if (!City) { return; }

	auto Addiction = [](int32 Home) -> float
	{
		float R = 0.f, L = 0.f, A = 0.f;
		UNpcRegistryComponent::PredictPersonality(FName(*FString::Printf(TEXT("Resident_%04d"), Home)), R, L, A);
		return A;
	};
	auto HasAppt = [this](int32 Home) -> bool
	{
		const ACustomerBase* C = FindResidentByHome(Home);
		return C && C->HasActiveAppointment();
	};

	if (bNight)
	{
		// 1) Niet-verslaafde roamers (zonder afspraak) van straat halen.
		for (int32 Home : PhysicalHomes.Array())
		{
			if (Addiction(Home) < NightAddictThreshold && !HasAppt(Home)) { DespawnResidentByHome(Home); }
		}
		// 2) Verslaafde bewoners naar buiten tot ~NightRoamers.
		TArray<int32> AddictPool;
		for (int32 Home : EligibleHomes)
		{
			if (!PhysicalHomes.Contains(Home) && Addiction(Home) >= NightAddictThreshold) { AddictPool.Add(Home); }
		}
		while (PhysicalHomes.Num() < NightRoamers && AddictPool.Num() > 0)
		{
			const int32 Idx = FMath::RandRange(0, AddictPool.Num() - 1);
			const int32 Home = AddictPool[Idx]; AddictPool.RemoveAtSwap(Idx);
			SpawnOneResident(City, Home, false);
		}
		// 3) Te veel? Schaaf terug tot NightRoamers (alleen zonder afspraak).
		if (PhysicalHomes.Num() > NightRoamers)
		{
			for (int32 Home : PhysicalHomes.Array())
			{
				if (PhysicalHomes.Num() <= NightRoamers) { break; }
				if (!HasAppt(Home)) { DespawnResidentByHome(Home); }
			}
		}
	}
	else
	{
		// Dag: crowd weer aanvullen tot MaxResidents (nieuwe gezichten eerst). NIET alles in één frame: alleen
		// een klein begin-batchje hier; de rest vult CheckResidentRotation geleidelijk bij (geen spawn-burst ->
		// geen samen-tollen na de dag/nacht-switch).
		const int32 DayTarget = (MaxResidents > 0) ? MaxResidents : EligibleHomes.Num();
		TArray<int32> ActNew, ActOld;
		for (int32 Home : EligibleHomes)
		{
			if (PhysicalHomes.Contains(Home)) { continue; }
			if (ActivatedEverHomes.Contains(Home)) { ActOld.Add(Home); } else { ActNew.Add(Home); }
		}
		int32 Batch = 0;
		while (PhysicalHomes.Num() < DayTarget && Batch < 6 && (ActNew.Num() + ActOld.Num()) > 0)
		{
			TArray<int32>& Pool = (ActNew.Num() > 0) ? ActNew : ActOld;
			const int32 Idx = FMath::RandRange(0, Pool.Num() - 1);
			const int32 Home = Pool[Idx]; Pool.RemoveAtSwap(Idx);
			SpawnOneResident(City, Home, false);
			++Batch;
		}
		NextDayRefillTime = World->GetTimeSeconds() + 0.5f;
	}
	ResidentHomeIndices = PhysicalHomes;
}

ACustomerBase* ACustomerSpawner::FindResidentByHome(int32 HomeIndex) const
{
	const FName Id(*FString::Printf(TEXT("Resident_%04d"), HomeIndex));
	for (TActorIterator<ACustomerBase> It(GetWorld()); It; ++It)
	{
		if (IsValid(*It) && It->NpcId == Id) { return *It; }
	}
	return nullptr;
}

void ACustomerSpawner::DespawnResidentByHome(int32 HomeIndex)
{
	// Niet hard verwijderen: loop eerst rustig naar je eigen huis en despawn pas binnen (geen plop op straat).
	if (ACustomerBase* C = FindResidentByHome(HomeIndex)) { C->SendHomeAndDespawn(); }
	PhysicalHomes.Remove(HomeIndex);
}

ACustomerBase* ACustomerSpawner::SpawnOneResident(ACityGenerator* City, int32 HomeIndex, bool bGuaranteedBuyer)
{
	UWorld* World = GetWorld();
	if (!World || !City) { return nullptr; }
	const TArray<FApartmentHome>& Homes = City->GetApartmentHomes();
	if (!Homes.IsValidIndex(HomeIndex)) { return nullptr; }
	const FApartmentHome& H = Homes[HomeIndex];

	AWeedShopGameState* GS = World->GetGameState<AWeedShopGameState>();
	UNpcRegistryComponent* Reg = GS ? GS->GetNpcRegistry() : nullptr;
	TSubclassOf<ACustomerBase> Cls = CustomerClass;
	if (!Cls) { Cls = ACustomerBase::StaticClass(); }

	const FString DoorName = ResidentNameByIndex(HomeIndex);
	const FName ResidentNpcId(*FString::Printf(TEXT("Resident_%04d"), HomeIndex));
	if (Reg) { Reg->EnsureNpc(ResidentNpcId, FText::FromString(DoorName)); }

	const FTransform SpawnTM(FRotator::ZeroRotator, H.InteriorPos + FVector(0.f, 0.f, 4.f));
	ACustomerBase* C = World->SpawnActorDeferred<ACustomerBase>(
		Cls, SpawnTM, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
	if (!C) { return nullptr; }
	C->NpcId = Reg ? ResidentNpcId : NAME_None;
	C->FinishSpawning(SpawnTM);
	C->SetupResident(H.DoorPos, H.InteriorPos, H.Number, H.HallPos);
	if (bGuaranteedBuyer) { C->Respect = 70.f; C->Loyalty = 40.f; C->Addiction = 80.f; C->BecomeBuyerNow(); }
	if (!C->GetController()) { C->SpawnDefaultController(); }
	PhysicalHomes.Add(HomeIndex);
	ActivatedEverHomes.Add(HomeIndex);
	return C;
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

void ACustomerSpawner::RotateResidents()
{
	UWorld* World = GetWorld();
	if (!World) { return; }
	ACityGenerator* City = nullptr;
	for (TActorIterator<ACityGenerator> It(World); It; ++It) { City = *It; break; }
	if (!City) { return; }

	// 1) Retire-kandidaten: huidige fysieke bewoners ZONDER lopende afspraak (anders blijven ze).
	TArray<int32> Retire;
	for (int32 HomeIdx : PhysicalHomes)
	{
		const ACustomerBase* C = FindResidentByHome(HomeIdx);
		if (C && C->HasActiveAppointment()) { continue; } // beschermd
		Retire.Add(HomeIdx);
	}
	// 2) Activeer-kandidaten: virtuele woningen, nog-niet-getoonde EERST.
	TArray<int32> ActNew, ActOld;
	for (int32 HomeIdx : EligibleHomes)
	{
		if (PhysicalHomes.Contains(HomeIdx)) { continue; }
		if (ActivatedEverHomes.Contains(HomeIdx)) { ActOld.Add(HomeIdx); } else { ActNew.Add(HomeIdx); }
	}
	if (Retire.Num() == 0 || (ActNew.Num() + ActOld.Num()) == 0) { return; }

	const int32 N = FMath::Min3(RotatePerDay, Retire.Num(), ActNew.Num() + ActOld.Num());

	// Willekeurige selectie (variatie per dag).
	auto PickRandom = [](TArray<int32>& Pool) -> int32
	{
		const int32 Idx = FMath::RandRange(0, Pool.Num() - 1);
		const int32 V = Pool[Idx];
		Pool.RemoveAtSwap(Idx);
		return V;
	};

	for (int32 i = 0; i < N; ++i)
	{
		// Despawn een willekeurige niet-beschermde fysieke bewoner.
		const int32 OutHome = PickRandom(Retire);
		DespawnResidentByHome(OutHome);

		// Activeer een virtuele woning: nieuwe gezichten eerst.
		const int32 InHome = (ActNew.Num() > 0) ? PickRandom(ActNew) : PickRandom(ActOld);
		SpawnOneResident(City, InHome, false);
	}
	ResidentHomeIndices = PhysicalHomes;
	UE_LOG(LogWeedShop, Log, TEXT("Resident rotatie: %d gewisseld (fysiek=%d, getoond-ooit=%d/%d)"),
		N, PhysicalHomes.Num(), ActivatedEverHomes.Num(), EligibleHomes.Num());
}

void ACustomerSpawner::SpawnHomeAndShopFixtures(ACityGenerator* City)
{
	UWorld* World = GetWorld();
	if (!World || !City || !HasAuthority()) { return; }
	// Alleen op een VERSE game; bij load herstelt de save-subsystem de geplaatste objecten zelf.
	if (UGameInstance* GI = World->GetGameInstance())
	{
		if (USaveGameSubsystem* Sv = GI->GetSubsystem<USaveGameSubsystem>())
		{
			if (!Sv->IsFreshGame()) { return; }
		}
	}

	auto FloorZ = [&](const FVector& At, float Fallback) -> float
	{
		FHitResult Hit;
		const FVector S(At.X, At.Y, At.Z + 300.f);
		const FVector E = S - FVector(0.f, 0.f, 1500.f);
		FCollisionQueryParams Q(FName(TEXT("FixtureFloor")), false);
		return World->LineTraceSingleByChannel(Hit, S, E, ECC_WorldStatic, Q) ? Hit.ImpactPoint.Z : Fallback;
	};
	auto SpawnProp = [&](FName ItemId, const FVector& Loc, float Yaw, bool bCosmetic)
	{
		const FTransform TM(FRotator(0.f, Yaw, 0.f), Loc);
		APlaceableProp* P = World->SpawnActorDeferred<APlaceableProp>(APlaceableProp::StaticClass(), TM, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (P) { P->ItemId = ItemId; P->FinishSpawning(TM); P->Tags.Add(FName(TEXT("AutoFixture"))); if (bCosmetic) { P->Tags.Add(FName(TEXT("Cosmetic"))); } }
	};

	// --- Meubels in de woningen ---
	TArray<FCityPropertyOffer> Offers; City->GetPropertyOffers(Offers);
	const TArray<FApartmentHome>& Homes = City->GetApartmentHomes();

	// Koopbare/starter-offers = JOUW woningen -> interactief (oppakbaar). Alle bewoner-woningen ->
	// cosmetisch (niet oppakbaar), zodat alleen je eigen meubels te verplaatsen zijn.
	TSet<int32> OfferSet;
	for (const FCityPropertyOffer& O : Offers) { for (int32 Idx : O.Homes) { OfferSet.Add(Idx); } }
	TSet<int32> Furnish = OfferSet;
	for (int32 Idx : ResidentHomeIndices) { Furnish.Add(Idx); }

	// Als de speler een eigen layout heeft opgeslagen (sandbox authoring), gebruik die PER TYPE; anders
	// de hardcoded standaard-set.
	TMap<FString, TArray<FFurnitureEntry>> Templates;
	const bool bHaveTemplates = FurnitureTemplates::LoadTemplates(Templates);

	// Diagnostiek: welke woning-sleutels komen voor en matchen ze de templates?
	TMap<FString, int32> KeyCount; TSet<FString> KeyMatched; int32 NTpl = 0, NFb = 0;

	for (int32 Idx : Furnish)
	{
		if (!Homes.IsValidIndex(Idx)) { continue; }
		const FApartmentHome& H = Homes[Idx];
		const FVector C = H.InteriorPos; const FVector R = H.RoomHalf;
		const bool bCosmetic = !OfferSet.Contains(Idx); // NPC-woning -> cosmetisch; jouw woning -> interactief

		const FString Type = FurnitureTemplates::TypeKey(H.bApartment, H.RoomHalf);
		KeyCount.FindOrAdd(Type)++;
		if (bHaveTemplates)
		{
			if (const TArray<FFurnitureEntry>* Entries = Templates.Find(Type))
			{
				for (const FFurnitureEntry& E : *Entries)
				{
					if (AActor* A = FurnitureTemplates::SpawnEntry(World, E, C, R, bCosmetic)) { A->Tags.Add(FName(TEXT("AutoFixture"))); }
				}
				KeyMatched.Add(Type); ++NTpl;
				continue;
			}
			// Geen template voor dit type -> val terug op de standaard-set hieronder.
		}
		++NFb;

		auto At = [&](float fx, float fy) { FVector L = C + FVector(R.X * fx, R.Y * fy, 0.f); L.Z = FloorZ(L, C.Z) + 2.f; return L; };
		SpawnProp(FName(TEXT("Mattress")), At(-0.45f, -0.45f), 0.f, bCosmetic);
		SpawnProp(FName(TEXT("Fridge")),   At( 0.45f,  0.45f), 180.f, bCosmetic);
		SpawnProp(FName(TEXT("Table")),    At( 0.0f,   0.40f), 0.f, bCosmetic);
		// Gootsteen = AWaterSink (eigen class); mesh-pivot in het midden -> ~halve hoogte omhoog.
		{
			FVector SinkLoc = At(0.45f, -0.45f); SinkLoc.Z += 45.f;
			FActorSpawnParameters SkSP; SkSP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AWaterSink* Sink = World->SpawnActor<AWaterSink>(AWaterSink::StaticClass(), FTransform(FRotator(0.f, 90.f, 0.f), SinkLoc), SkSP);
			if (Sink) { Sink->Tags.Add(FName(TEXT("Cosmetic"))); Sink->Tags.Add(FName(TEXT("AutoFixture"))); } // vaste fixture + niet in capture
		}
	}

	// Diagnostiek-log: vergelijk de echte woning-sleutels met de beschikbare template-sleutels.
	{
		FString Avail;
		for (const TPair<FString, TArray<FFurnitureEntry>>& KV : Templates) { Avail += KV.Key + TEXT(" "); }
		UE_LOG(LogWeedShop, Warning, TEXT("FurnishDiag: %d homes | %d via TEMPLATE, %d via fallback | template-keys=[%s]"),
			NTpl + NFb, NTpl, NFb, *Avail);
		for (const TPair<FString, int32>& KV : KeyCount)
		{
			UE_LOG(LogWeedShop, Warning, TEXT("FurnishDiag:   home-type %-12s x%-3d %s"),
				*KV.Key, KV.Value, KeyMatched.Contains(KV.Key) ? TEXT("[TEMPLATE]") : TEXT("[fallback->random]"));
		}
	}

	// (ATM's worden nu door CityGenerator naast de balie in elke winkel geplaatst - betrouwbaarder
	//  dan de oude gok-positie op het blok-midden, die in een muur/buiten kon belanden.)
}

void ACustomerSpawner::SetupTestCustomer(ACustomerBase* C, const FVector& Park)
{
	if (!IsValid(C)) { return; }
	if (C->PatienceSeconds > 1.0e8f) { return; } // al ingesteld als test-klant

	C->Respect = 70.f; C->Loyalty = 40.f; C->Addiction = 80.f;
	C->bDespawnAfterServed = false;
	C->BecomeBuyerNow();
	C->PatienceSeconds = 1.0e9f; // vertrekt niet tijdens testen
	C->SetActorLocation(Park);
	C->SetHome(Park);
	C->SetSpot(Park);
	if (!C->GetController()) { C->SpawnDefaultController(); }
	C->WalkTo(Park);
}
