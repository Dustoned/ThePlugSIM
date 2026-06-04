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
		static const TCHAR* First[] = {
			TEXT("Jan"), TEXT("Piet"), TEXT("Kees"), TEXT("Sanne"), TEXT("Emma"), TEXT("Daan"), TEXT("Lotte"), TEXT("Bram"),
			TEXT("Sven"), TEXT("Fleur"), TEXT("Tim"), TEXT("Noa"), TEXT("Rick"), TEXT("Iris"), TEXT("Joost"), TEXT("Mila"),
			TEXT("Gerrit"), TEXT("Truus"), TEXT("Henk"), TEXT("Willem"), TEXT("Bep"), TEXT("Cor"), TEXT("Sjaak"), TEXT("Ria"),
			TEXT("Dirk"), TEXT("Mieke"), TEXT("Bart"), TEXT("Loes"), TEXT("Wout"), TEXT("Stijn"), TEXT("Femke"), TEXT("Jasper"),
			TEXT("Roos"), TEXT("Teun"), TEXT("Saar"), TEXT("Koen"), TEXT("Hilda"), TEXT("Bennie"), TEXT("Manon"), TEXT("Ferry"),
			TEXT("Chantal"), TEXT("Bertus"), TEXT("Sandra"), TEXT("Ronnie"), TEXT("Yvonne"), TEXT("Gijs"), TEXT("Niels"), TEXT("Maud"),
			TEXT("Tessa"), TEXT("Luuk"), TEXT("Bo"), TEXT("Sam"), TEXT("Nina"), TEXT("Mees"), TEXT("Lars"), TEXT("Kim"),
			TEXT("Isa"), TEXT("Mats"), TEXT("Jill"), TEXT("Dex"), TEXT("Puck"), TEXT("Guus"), TEXT("Floor"), TEXT("Ravi"),
			TEXT("Nova"), TEXT("Otis"), TEXT("Liva"), TEXT("Moos"), TEXT("Tijn"), TEXT("Sofie"), TEXT("Fien"), TEXT("Rens"),
			TEXT("Jules"), TEXT("Lio"), TEXT("Morris"), TEXT("Evi"), TEXT("Tuur"), TEXT("Vera"), TEXT("Siem"), TEXT("Luna"),
			TEXT("Mick"), TEXT("Cato"), TEXT("Boaz"), TEXT("Pippa"), TEXT("Ruben"), TEXT("Mara"), TEXT("Tobias"), TEXT("Lieke"),
			TEXT("Jurre"), TEXT("Nora"), TEXT("Cas"), TEXT("Elin"), TEXT("Sil"), TEXT("Myrthe"), TEXT("Diede"), TEXT("Anne"),
			TEXT("Ome Ton"), TEXT("Tante An"), TEXT("Appie"), TEXT("Non"), TEXT("Ouwe Joop"), TEXT("Dikke Leo"), TEXT("Tinus"),
			TEXT("Sjonnie"), TEXT("Annie"), TEXT("Bennie Bob"), TEXT("Kleine Kees"), TEXT("Tante Sjaan") };
		static const TCHAR* Last[] = {
			TEXT("Pannenkoek"), TEXT("Stokvis"), TEXT("Bonk"), TEXT("Knol"), TEXT("Prummel"), TEXT("Druif"), TEXT("Kwast"),
			TEXT("Worst"), TEXT("Toeter"), TEXT("Boterham"), TEXT("Stamppot"), TEXT("Frikandel"), TEXT("Klont"), TEXT("Hark"),
			TEXT("Sok"), TEXT("Krent"), TEXT("Pruim"), TEXT("Brok"), TEXT("Plof"), TEXT("Kwakkel"), TEXT("Schroef"),
			TEXT("Knapzak"), TEXT("Peul"), TEXT("Klaproos"), TEXT("Bonenstaak"), TEXT("Kaaskop"), TEXT("Mosterd"),
			TEXT("Pareltje"), TEXT("Stronk"), TEXT("Knaak"), TEXT("Krentenbol"), TEXT("Pindakaas"), TEXT("Knetter"),
			TEXT("Slinger"), TEXT("Fluitketel"), TEXT("Stoeptegel"), TEXT("Koffiemok"), TEXT("Dropveter"), TEXT("Bamischijf"),
			TEXT("Kapsalon"), TEXT("Draaitafel"), TEXT("Plakband"), TEXT("Kruik"), TEXT("Waslijn"), TEXT("Kruimel"),
			TEXT("Sjoelbak"), TEXT("Tosti"), TEXT("Klodder"), TEXT("Vlaai"), TEXT("Kiekeboe"), TEXT("Nattevinger"),
			TEXT("Knalpot"), TEXT("Glitterjas"), TEXT("Poffertje"), TEXT("Klusbus"), TEXT("Zilveruitje"), TEXT("Limonade"),
			TEXT("Trommel"), TEXT("Badmuts"), TEXT("Knipperlicht"), TEXT("Kaasplank"), TEXT("Hagelslag"), TEXT("Frietzak"),
			TEXT("Spekkoek"), TEXT("Schuimkraag"), TEXT("Kluitje"), TEXT("Kratje"), TEXT("Wafelijzer"), TEXT("Smeerkaas"),
			TEXT("Drol"), TEXT("Bil"), TEXT("Pielewaaier"), TEXT("Zeurpiet"), TEXT("Slok"), TEXT("Tuthola"),
			TEXT("Snor"), TEXT("Krakeling"), TEXT("Prutser"), TEXT("Klodderkont"), TEXT("Natte Krant"), TEXT("Snotneus"),
			TEXT("Kletsmajoor"), TEXT("Boterletter"), TEXT("Kruimeldief"), TEXT("Mallemolen") };
		const int32 NF = (int32)UE_ARRAY_COUNT(First);
		const int32 NL = (int32)UE_ARRAY_COUNT(Last);
		const int32 I = FMath::Max(0, Index);
		return FString::Printf(TEXT("%s %s"), First[I % NF], Last[(I * 37 + I / NF) % NL]);
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
		bool bAdded = false;
		for (const FIntPoint& Key : CellOrder)
		{
			if (made >= UWant) { break; }
			const TArray<int32>& Arr = Cells[Key];
			if (round < Arr.Num()) { PhysicalSet.Add(Arr[round]); ++made; bAdded = true; }
		}
		if (!bAdded) { break; } // alle cellen leeg -> klaar
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

			const FTransform SpawnTM(FRotator::ZeroRotator, H.DoorPos);
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
