#include "World/WorldSyncComponent.h"
#include "Net/UnrealNetwork.h"

UWorldSyncComponent::UWorldSyncComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UWorldSyncComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UWorldSyncComponent, OpenDoors);
}

uint32 UWorldSyncComponent::MakeId(const FVector& Loc, float Yaw)
{
	// Afronden op een 10cm-grid + 1-graden-yaw zodat kleine float-verschillen host/client geen ander id geven.
	// Posities/yaws zijn deterministisch identiek op elke machine (DoorRetrofitter), dus dit id
	// is overal hetzelfde. FNV-1a hash voor een stabiele, deterministische waarde (geen GetTypeHash-grilligheid).
	const int32 X = FMath::RoundToInt(Loc.X / 10.f);
	const int32 Y = FMath::RoundToInt(Loc.Y / 10.f);
	const int32 Z = FMath::RoundToInt(Loc.Z / 10.f);
	const int32 W = FMath::RoundToInt(FRotator::ClampAxis(Yaw));
	uint32 H = 2166136261u;
	auto Mix = [&H](int32 V) { const uint32 U = (uint32)V; for (int i = 0; i < 4; ++i) { H = (H ^ ((U >> (i * 8)) & 0xFF)) * 16777619u; } };
	Mix(X); Mix(Y); Mix(Z); Mix(W);
	return H ? H : 1u;
}

void UWorldSyncComponent::ServerToggleDoor(uint32 DoorId)
{
	if (GetOwnerRole() != ROLE_Authority || DoorId == 0) { return; }
	if (OpenDoors.Contains(DoorId)) { OpenDoors.Remove(DoorId); }
	else { OpenDoors.Add(DoorId); }
}

void UWorldSyncComponent::ServerSetDoor(uint32 DoorId, bool bOpen)
{
	if (GetOwnerRole() != ROLE_Authority || DoorId == 0) { return; }
	if (bOpen) { OpenDoors.AddUnique(DoorId); }
	else { OpenDoors.Remove(DoorId); }
}
