#include "Customer/CustomerSpawner.h"

#include "WeedShopCore.h"
#include "Customer/CustomerBase.h"
#include "World/CityGenerator.h"
#include "World/CityDoor.h"
#include "Game/WeedShopGameState.h"
#include "Npc/NpcRegistryComponent.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerStart.h"
#include "Placement/PlaceableProp.h"
#include "World/Atm.h"
#include "World/WaterSink.h"
#include "Save/SaveGameSubsystem.h"
#include "Engine/GameInstance.h"
#include "CollisionQueryParams.h"
#include "NavigationSystem.h"

ACustomerSpawner::ACustomerSpawner()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bTickEvenWhenPaused = true;
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
		if (Snapshot.bStuckSuspect) { ++StuckSuspect; }
		if (Snapshot.bNearMapEdge) { ++NearEdge; }
		const bool bNoOutdoorGoal = !Snapshot.bAtHomeInside && !Snapshot.bEmergingFromHome && !Snapshot.bEnteringHome
			&& !Snapshot.bHasGoal && Snapshot.NoGoalSeconds > 6.f;
		const bool bParkUrgentWithoutParkGoal = Snapshot.bParkUrgentToday && !Snapshot.bGoalIsPark && !Snapshot.bParkPause;
		if (!Snapshot.bOnSidewalkOrPark || Snapshot.bStuckSuspect || bNoOutdoorGoal || bParkUrgentWithoutParkGoal)
		{
			++IssueCount;
			if (IssueDetails.Num() < 6)
			{
				FString Flags;
				if (!Snapshot.bOnSidewalkOrPark) { Flags += TEXT("offSidewalk,"); }
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
		TEXT("Resident movement monitor: sample=%d total=%d visible=%d outdoor=%d emerging=%d entering=%d moving=%d goals=%d parkActive=%d parkDoneToday=%d parkNeedsToday=%d offSidewalk=%d stuck=%d nearEdge=%d cells=%d"),
		SampleIndex, Total, Visible, Outdoor, Emerging, Entering, Moving, WithGoal, ParkActive, ParkDoneToday, ParkNeedsToday,
		OffSidewalk, StuckSuspect, NearEdge, OccupiedCells.Num());
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

	// Verlopen klanten opruimen.
	Spawned.RemoveAll([](const TObjectPtr<ACustomerBase>& C) { return !IsValid(C); });

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
			TEXT("Jan"), TEXT("Piet"), TEXT("Kees"), TEXT("Sanne"), TEXT("Emma"), TEXT("Daan"), TEXT("Lotte"), TEXT("Bram"),
			TEXT("Sven"), TEXT("Fleur"), TEXT("Tim"), TEXT("Noa"), TEXT("Rick"), TEXT("Iris"), TEXT("Joost"), TEXT("Mila"),
			TEXT("Henk"), TEXT("Willem"), TEXT("Bep"), TEXT("Cor"), TEXT("Ria"), TEXT("Dirk"), TEXT("Mieke"), TEXT("Bart"),
			TEXT("Loes"), TEXT("Wout"), TEXT("Stijn"), TEXT("Femke"), TEXT("Jasper"), TEXT("Roos"), TEXT("Teun"), TEXT("Saar"),
			TEXT("Koen"), TEXT("Manon"), TEXT("Gijs"), TEXT("Niels"), TEXT("Maud"), TEXT("Tessa"), TEXT("Luuk"), TEXT("Bo"),
			TEXT("Sam"), TEXT("Nina"), TEXT("Mees"), TEXT("Lars"), TEXT("Kim"), TEXT("Isa"), TEXT("Mats"), TEXT("Jill"),
			TEXT("Dex"), TEXT("Puck"), TEXT("Guus"), TEXT("Floor"), TEXT("Ravi"), TEXT("Nova"), TEXT("Otis"), TEXT("Liva"),
			TEXT("Moos"), TEXT("Tijn"), TEXT("Sofie"), TEXT("Fien"), TEXT("Rens"), TEXT("Jules"), TEXT("Lio"), TEXT("Evi"),
			TEXT("Vera"), TEXT("Siem"), TEXT("Luna"), TEXT("Mick"), TEXT("Cato"), TEXT("Boaz"), TEXT("Ruben"), TEXT("Mara"),
			TEXT("Lieke"), TEXT("Nora"), TEXT("Cas"), TEXT("Elin"), TEXT("Sil"), TEXT("Anne"), TEXT("Tess"), TEXT("Britt") };
		static const TCHAR* Last[] = {
			TEXT("Bakker"), TEXT("Bos"), TEXT("Vos"), TEXT("Smit"), TEXT("Meijer"), TEXT("Visser"), TEXT("Jansen"), TEXT("Dekker"),
			TEXT("Mulder"), TEXT("Kok"), TEXT("Boer"), TEXT("Dijkstra"), TEXT("Post"), TEXT("Klein"), TEXT("Prins"), TEXT("Kuiper"),
			TEXT("Brand"), TEXT("Vink"), TEXT("Blom"), TEXT("Molen"), TEXT("Linden"), TEXT("Vries"), TEXT("Wit"), TEXT("Zwaan"),
			TEXT("Koster"), TEXT("Verhoef"), TEXT("Maas"), TEXT("Wolff"), TEXT("Hof"), TEXT("Kroon"), TEXT("Timmer"), TEXT("Groen"),
			TEXT("Hoek"), TEXT("Mars"), TEXT("Berg"), TEXT("Roos"), TEXT("Kramer"), TEXT("Peters"), TEXT("Hendriks"), TEXT("Schol") };
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
	// deur als "TE KOOP" totdat de speler 'm koopt (dan ontgrendelt de PhoneClientComponent 'm lokaal).
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

	AWeedShopGameState* GS = World->GetGameState<AWeedShopGameState>();
	UNpcRegistryComponent* Reg = GS ? GS->GetNpcRegistry() : nullptr;

	TSubclassOf<ACustomerBase> Cls = CustomerClass;
	if (!Cls) { Cls = ACustomerBase::StaticClass(); }

	// Fysieke roamers kiezen we per UNIEKE hoofdingang: alle units van één appartement-/flatgebouw delen
	// dezelfde DoorPos, dus zonder dit zouden er tig bewoners op exact dezelfde stoephoek clusteren.
	// Eén roamer per ingang, gespreid over de stad.
	auto DoorKey = [](const FVector& P) { return FIntVector(FMath::RoundToInt(P.X / 50.f), FMath::RoundToInt(P.Y / 50.f), 0); };
	TArray<int32> Entrances; // eerste home-index per unieke DoorPos (geen koopbare panden)
	{
		TSet<FIntVector> Seen;
		for (int32 i = 0; i < Total; ++i)
		{
			if (ForSale.Contains(i)) { continue; }
			const FIntVector Key = DoorKey(Homes[i].DoorPos);
			if (!Seen.Contains(Key)) { Seen.Add(Key); Entrances.Add(i); }
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

	// Bij een verse game: meubels in de koopbare woningen + ATM in elke winkel (server-side, repliceert).
	SpawnHomeAndShopFixtures(City);
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
	auto SpawnProp = [&](FName ItemId, const FVector& Loc, float Yaw)
	{
		const FTransform TM(FRotator(0.f, Yaw, 0.f), Loc);
		APlaceableProp* P = World->SpawnActorDeferred<APlaceableProp>(APlaceableProp::StaticClass(), TM, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (P) { P->ItemId = ItemId; P->FinishSpawning(TM); }
	};

	// --- Meubels in elke koopbare/starter-woning ---
	TArray<FCityPropertyOffer> Offers; City->GetPropertyOffers(Offers);
	const TArray<FApartmentHome>& Homes = City->GetApartmentHomes();
	TSet<int32> Done;
	for (const FCityPropertyOffer& O : Offers)
	{
		for (int32 Idx : O.Homes)
		{
			if (Done.Contains(Idx) || !Homes.IsValidIndex(Idx)) { continue; }
			Done.Add(Idx);
			const FApartmentHome& H = Homes[Idx];
			const FVector C = H.InteriorPos; const FVector R = H.RoomHalf;
			auto At = [&](float fx, float fy) { FVector L = C + FVector(R.X * fx, R.Y * fy, 0.f); L.Z = FloorZ(L, C.Z) + 2.f; return L; };
			SpawnProp(FName(TEXT("Mattress")), At(-0.45f, -0.45f), 0.f);
			SpawnProp(FName(TEXT("Fridge")),   At( 0.45f,  0.45f), 180.f);
			SpawnProp(FName(TEXT("Table")),    At( 0.0f,   0.40f), 0.f);
			// Gootsteen = AWaterSink (eigen class); mesh-pivot in het midden -> ~halve hoogte omhoog.
			{
				FVector SinkLoc = At(0.45f, -0.45f); SinkLoc.Z += 45.f;
				World->SpawnActor<AWaterSink>(AWaterSink::StaticClass(), FTransform(FRotator(0.f, 90.f, 0.f), SinkLoc));
			}
		}
	}

	// --- ATM in elke winkel (binnen) ---
	TArray<FCityMapBlock> Blocks; City->GetMapBlocks(Blocks);
	const FVector Center = City->GetCityCenter();
	for (const FCityMapBlock& B : Blocks)
	{
		if (!B.bShop) { continue; }
		FVector L(B.Center.X + 160.f, B.Center.Y + 160.f, Center.Z); // iets uit het midden (niet in de balie)
		L.Z = FloorZ(L, Center.Z - 90.f) + 2.f;
		World->SpawnActor<AAtm>(AAtm::StaticClass(), FTransform(FRotator::ZeroRotator, L));
	}
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
