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
	const int32 Cap = bTestSingleHighStat ? 1 : MaxCustomers;
	if (Spawned.Num() >= Cap) { return; }

	TSubclassOf<ACustomerBase> Cls = CustomerClass;
	if (!Cls) { Cls = ACustomerBase::StaticClass(); }

	// Test: spawn in het centrale park (rond de PlayerStart); anders op het spawner-punt.
	FVector Base = GetActorLocation();
	if (bTestSingleHighStat)
	{
		for (TActorIterator<APlayerStart> It(World); It; ++It) { Base = It->GetActorLocation(); break; }
		Base += FVector(300.f, 300.f, 0.f); // netjes naast het pad in het park
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	ACustomerBase* C = World->SpawnActor<ACustomerBase>(Cls, Base, GetActorRotation(), Params);
	if (!C) { return; }

	if (bTestSingleHighStat)
	{
		// Hoge stat -> meteen een kopende klant; blijft staan in het park en vertrekt niet.
		C->Respect = 70.f; C->Loyalty = 40.f; C->Addiction = 80.f;
		C->bDespawnAfterServed = false;
		C->BecomeBuyerNow();
		C->PatienceSeconds = 1.0e9f; // geen geduld-vertrek tijdens testen
		C->SetHome(Base);
		C->SetSpot(Base);
		if (!C->GetController()) { C->SpawnDefaultController(); }
		C->WalkTo(Base);
	}
	else
	{
		// Plek in de buurt (willekeurige hoek/afstand) + thuis = spawn-punt.
		const float Ang = FMath::FRandRange(0.f, 2.f * PI);
		const float Dist = FMath::FRandRange(150.f, FMath::Max(160.f, SpotRadius));
		const FVector Spot = Base + FVector(FMath::Cos(Ang) * Dist, FMath::Sin(Ang) * Dist, 0.f);
		C->SetHome(Base);
		C->SetSpot(Spot);
		if (!C->GetController()) { C->SpawnDefaultController(); }
		C->WalkTo(Spot);
	}

	Spawned.Add(C);
}
