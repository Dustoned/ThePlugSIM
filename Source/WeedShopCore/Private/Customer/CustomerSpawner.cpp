#include "Customer/CustomerSpawner.h"

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

ACustomerSpawner::ACustomerSpawner()
{
	PrimaryActorTick.bCanEverTick = false;
}

void ACustomerSpawner::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority())
	{
		// Snel retry-interval zodat de bewoners verschijnen zodra de stad gebouwd is.
		GetWorldTimerManager().SetTimer(SpawnTimer, this, &ACustomerSpawner::TrySpawn, 2.0f, true, 1.0f);
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
}

namespace
{
	// Unieke NL-bewonersnaam per woning. Op INDEX (niet hash) -> elke woning een eigen voor+achternaam
	// zonder botsingen, tot First*Last combinaties. Grote pools voor veel variatie.
	FString ResidentNameByIndex(int32 Index)
	{
		static const TCHAR* First[] = { TEXT("Jan"), TEXT("Piet"), TEXT("Kees"), TEXT("Sanne"), TEXT("Emma"), TEXT("Daan"),
			TEXT("Lotte"), TEXT("Bram"), TEXT("Sven"), TEXT("Fleur"), TEXT("Tim"), TEXT("Noa"), TEXT("Rick"), TEXT("Iris"),
			TEXT("Joost"), TEXT("Mila"), TEXT("Gerrit"), TEXT("Truus"), TEXT("Henk"), TEXT("Willem"), TEXT("Bep"), TEXT("Cor"),
			TEXT("Sjaak"), TEXT("Ria"), TEXT("Dirk"), TEXT("Mieke"), TEXT("Bart"), TEXT("Loes"), TEXT("Ome Ton"), TEXT("Tante An"),
			TEXT("Wout"), TEXT("Stijn"), TEXT("Femke"), TEXT("Jasper"), TEXT("Roos"), TEXT("Teun"), TEXT("Saar"), TEXT("Koen"),
			TEXT("Hilda"), TEXT("Bennie"), TEXT("Manon"), TEXT("Ferry"), TEXT("Non"), TEXT("Appie"), TEXT("Chantal"), TEXT("Bertus"),
			TEXT("Sandra"), TEXT("Ronnie"), TEXT("Yvonne"), TEXT("Gijs") };
		static const TCHAR* Last[] = { TEXT("Pannenkoek"), TEXT("Stokvis"), TEXT("Bonk"), TEXT("Knol"), TEXT("Prummel"),
			TEXT("Druif"), TEXT("Kwast"), TEXT("Worst"), TEXT("Toeter"), TEXT("Boterham"), TEXT("Stamppot"), TEXT("Frikandel"),
			TEXT("Snoek"), TEXT("Klont"), TEXT("Hark"), TEXT("Sok"), TEXT("Krent"), TEXT("Pruim"), TEXT("Brok"), TEXT("Plof"),
			TEXT("Kwakkel"), TEXT("Tuthola"), TEXT("Schroef"), TEXT("Knapzak"), TEXT("Bil"), TEXT("Drol"), TEXT("Snor"), TEXT("Krakeling"),
			TEXT("Peul"), TEXT("Zeurpiet"), TEXT("Klaproos"), TEXT("Bonenstaak"), TEXT("Kaaskop"), TEXT("Mosterd"), TEXT("Slok"),
			TEXT("Brombeer"), TEXT("Pareltje"), TEXT("Stronk"), TEXT("Knaak"), TEXT("Pielewaaier") };
		const int32 NF = (int32)UE_ARRAY_COUNT(First);
		const int32 NL = (int32)UE_ARRAY_COUNT(Last);
		const int32 I = FMath::Max(0, Index);
		return FString::Printf(TEXT("%s %s"), First[I % NF], Last[(I / NF) % NL]);
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

	// ELKE genummerde woning krijgt een bewoner (deur op slot met naam). Een gespreide subset loopt
	// ook ECHT rond (MaxResidents; 0 = allemaal). De rest "woont er" via de op-slot-deur met naam.
	const int32 Total = Homes.Num();

	// De 3 koopbare panden (starter-flatje, rijtjeshuis, grote kamer) blijven ALTIJD vrij: geen bewoner,
	// deur als "TE KOOP" totdat de speler 'm koopt (dan ontgrendelt de PhoneClientComponent 'm lokaal).
	TArray<FCityPropertyOffer> Offers; City->GetPropertyOffers(Offers);
	TSet<int32> ForSale;
	for (const FCityPropertyOffer& O : Offers) { for (int32 Idx : O.Homes) { ForSale.Add(Idx); } }

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
	const int32 UStep = (UWant > 0) ? FMath::Max(1, Entrances.Num() / UWant) : (Entrances.Num() + 1);
	TSet<int32> PhysicalSet;
	for (int32 k = 0, made = 0; k < Entrances.Num() && made < UWant; k += UStep) { PhysicalSet.Add(Entrances[k]); ++made; }

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

		if (bPhysical)
		{
			FActorSpawnParameters SP;
			SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
			if (ACustomerBase* C = World->SpawnActor<ACustomerBase>(Cls, H.DoorPos, FRotator::ZeroRotator, SP))
			{
				if (Reg) { C->NpcId = Reg->AssignNpc(); } // registry voor stats/contact; deurnaam komt per-nummer
				C->SetupResident(H.DoorPos, H.InteriorPos, H.Number);
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
		const FString DoorName = ResidentNameByIndex(i);
		if (ACityDoor* Dr = H.Door.Get()) { Dr->SetResident(DoorName); }
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
