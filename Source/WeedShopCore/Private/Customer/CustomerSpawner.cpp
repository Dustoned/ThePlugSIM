#include "Customer/CustomerSpawner.h"

#include "Customer/CustomerBase.h"
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
