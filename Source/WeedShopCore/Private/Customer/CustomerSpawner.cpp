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

ACustomerSpawner::ACustomerSpawner()
{
	PrimaryActorTick.bCanEverTick = false;
}

void ACustomerSpawner::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority())
	{
		GetWorldTimerManager().SetTimer(SpawnTimer, this, &ACustomerSpawner::TrySpawn, SpawnInterval, true, 1.5f);
	}
}

void ACustomerSpawner::TrySpawn()
{
	UWorld* World = GetWorld();
	if (!World || !HasAuthority()) { return; }

	// Verlopen klanten opruimen.
	Spawned.RemoveAll([](const TObjectPtr<ACustomerBase>& C) { return !IsValid(C); });

	// Bewoner-modus heeft voorrang: ken NPC's toe aan appartementen zodra de stad gebouwd is.
	if (bSpawnResidents)
	{
		if (!bResidentsSpawned) { SpawnResidents(); }
		return;
	}

	if (bTestSingleHighStat)
	{
		// TEST: WERELDBREED precies ÉÉN klant (ongeacht hoeveel spawners er staan of klanten in de map).
		TArray<ACustomerBase*> All;
		for (TActorIterator<ACustomerBase> It(World); It; ++It) { if (IsValid(*It)) { All.Add(*It); } }

		// Plek: netjes in het centrale park (rond de PlayerStart).
		FVector Park = GetActorLocation();
		for (TActorIterator<APlayerStart> It(World); It; ++It) { Park = It->GetActorLocation(); break; }
		Park += FVector(300.f, 300.f, 0.f);

		if (All.Num() > 0)
		{
			// Houd er één; ruim de rest op.
			for (int32 k = 1; k < All.Num(); ++k) { if (IsValid(All[k])) { All[k]->Destroy(); } }
			SetupTestCustomer(All[0], Park);
			return;
		}

		// Geen klant -> spawn er één in het park.
		TSubclassOf<ACustomerBase> Cls = CustomerClass;
		if (!Cls) { Cls = ACustomerBase::StaticClass(); }
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
		if (ACustomerBase* C = World->SpawnActor<ACustomerBase>(Cls, Park, GetActorRotation(), Params))
		{
			SetupTestCustomer(C, Park);
		}
		return;
	}

	if (Spawned.Num() >= MaxCustomers) { return; }

	TSubclassOf<ACustomerBase> Cls = CustomerClass;
	if (!Cls) { Cls = ACustomerBase::StaticClass(); }
	const FVector Base = GetActorLocation();

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	ACustomerBase* C = World->SpawnActor<ACustomerBase>(Cls, Base, GetActorRotation(), Params);
	if (!C) { return; }

	// Plek in de buurt (willekeurige hoek/afstand) + thuis = spawn-punt.
	const float Ang = FMath::FRandRange(0.f, 2.f * PI);
	const float Dist = FMath::FRandRange(150.f, FMath::Max(160.f, SpotRadius));
	const FVector Spot = Base + FVector(FMath::Cos(Ang) * Dist, FMath::Sin(Ang) * Dist, 0.f);
	C->SetHome(Base);
	C->SetSpot(Spot);
	if (!C->GetController()) { C->SpawnDefaultController(); }
	C->WalkTo(Spot);

	Spawned.Add(C);
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

	AWeedShopGameState* GS = World->GetGameState<AWeedShopGameState>();
	UNpcRegistryComponent* Reg = GS ? GS->GetNpcRegistry() : nullptr;

	TSubclassOf<ACustomerBase> Cls = CustomerClass;
	if (!Cls) { Cls = ACustomerBase::StaticClass(); }

	// Gespreide subset van woningen kiezen (verschillende gebouwen/etages).
	const int32 Count = FMath::Clamp(MaxResidents, 0, Homes.Num());
	const int32 Step = FMath::Max(1, Homes.Num() / FMath::Max(1, Count));
	int32 Made = 0;
	for (int32 i = 0; i < Homes.Num() && Made < Count; i += Step)
	{
		const FApartmentHome& H = Homes[i];
		FActorSpawnParameters SP;
		SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
		ACustomerBase* C = World->SpawnActor<ACustomerBase>(Cls, H.DoorPos, FRotator::ZeroRotator, SP);
		if (!C) { continue; }

		// NPC-identiteit + naam (voor de slot-prompt).
		FString Name = H.Number;
		if (Reg)
		{
			C->NpcId = Reg->AssignNpc();
			float r = 0.f, l = 0.f, a = 0.f; FText N;
			if (!C->NpcId.IsNone() && Reg->GetStats(C->NpcId, r, l, a, N) && !N.IsEmpty()) { Name = N.ToString(); }
		}
		C->SetupResident(H.DoorPos, H.InteriorPos, H.Number);

		// Eerste bewoner = gegarandeerde koopklare test-klant.
		if (Made == 0)
		{
			C->Respect = 70.f; C->Loyalty = 40.f; C->Addiction = 80.f;
			C->BecomeBuyerNow();
		}

		// Appartementdeur op slot met de bewonersnaam ("LOCKED - <naam> lives here").
		if (ACityDoor* Dr = H.Door.Get()) { Dr->SetResident(Name); }

		if (!C->GetController()) { C->SpawnDefaultController(); }
		Spawned.Add(C);
		++Made;
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
