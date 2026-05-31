#include "Customer/CustomerSpawner.h"

#include "Customer/CustomerBase.h"
#include "Engine/World.h"
#include "TimerManager.h"

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
