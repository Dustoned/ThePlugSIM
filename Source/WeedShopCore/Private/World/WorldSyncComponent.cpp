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
	DOREPLIFETIME(UWorldSyncComponent, ElevatorIds);
	DOREPLIFETIME(UWorldSyncComponent, ElevatorFloors);
	DOREPLIFETIME(UWorldSyncComponent, ElevatorZ);
	DOREPLIFETIME(UWorldSyncComponent, WeatherIndex);
	DOREPLIFETIME(UWorldSyncComponent, WeatherDuration);
	DOREPLIFETIME(UWorldSyncComponent, LampIds);
	DOREPLIFETIME(UWorldSyncComponent, LampOn);
	DOREPLIFETIME(UWorldSyncComponent, LampBright);
	DOREPLIFETIME(UWorldSyncComponent, SwitchPos);
	DOREPLIFETIME(UWorldSyncComponent, SwitchYaw);
	DOREPLIFETIME(UWorldSyncComponent, bSwitchesPublished);
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

int32 UWorldSyncComponent::GetElevatorFloor(uint32 ElevId) const
{
	const int32 Idx = ElevatorIds.IndexOfByKey(ElevId);
	return (Idx != INDEX_NONE && ElevatorFloors.IsValidIndex(Idx)) ? ElevatorFloors[Idx] : INDEX_NONE;
}

void UWorldSyncComponent::ServerSetElevatorFloor(uint32 ElevId, int32 Floor)
{
	if (GetOwnerRole() != ROLE_Authority || ElevId == 0) { return; }
	const int32 Idx = ElevatorIds.IndexOfByKey(ElevId);
	if (Idx != INDEX_NONE)
	{
		if (ElevatorFloors.IsValidIndex(Idx)) { ElevatorFloors[Idx] = Floor; }
	}
	else
	{
		// Nieuw id: alle drie de parallelle arrays gelijk laten groeien (zelfde index). ElevatorZ start op 0;
		// SetElevatorZ (elke server-tick) schrijft er meteen de echte hoogte in.
		ElevatorIds.Add(ElevId);
		ElevatorFloors.Add(Floor);
		ElevatorZ.Add(0.f);
	}
}

float UWorldSyncComponent::GetElevatorZ(uint32 ElevId, float Fallback) const
{
	const int32 Idx = ElevatorIds.IndexOfByKey(ElevId);
	return (Idx != INDEX_NONE && ElevatorZ.IsValidIndex(Idx)) ? ElevatorZ[Idx] : Fallback;
}

void UWorldSyncComponent::SetElevatorZ(uint32 ElevId, float Z)
{
	if (GetOwnerRole() != ROLE_Authority || ElevId == 0) { return; }
	const int32 Idx = ElevatorIds.IndexOfByKey(ElevId);
	if (Idx != INDEX_NONE)
	{
		if (ElevatorZ.IsValidIndex(Idx)) { ElevatorZ[Idx] = Z; }
	}
	else
	{
		// Nieuw id (cabine schrijft z'n hoogte voordat er een verdieping-call was): alle drie de arrays gelijk
		// laten groeien. Floor onbekend -> INDEX_NONE, zodat GetElevatorFloor "geen server-doel" blijft geven.
		ElevatorIds.Add(ElevId);
		ElevatorFloors.Add(INDEX_NONE);
		ElevatorZ.Add(Z);
	}
}

void UWorldSyncComponent::SetWeather(int32 Index, float Duration)
{
	// WorldSync leeft op de GameState -> ROLE_Authority is hier de correcte server-check (ook voor de listen-host).
	if (GetOwnerRole() != ROLE_Authority) { return; }
	WeatherIndex = Index;
	WeatherDuration = Duration;
}

void UWorldSyncComponent::SetLampState(uint32 LampId, bool bOn, float Brightness01)
{
	if (GetOwnerRole() != ROLE_Authority || LampId == 0) { return; }
	const uint8 On = bOn ? 1 : 0;
	const int32 Idx = LampIds.IndexOfByKey(LampId);
	if (Idx != INDEX_NONE)
	{
		if (LampOn.IsValidIndex(Idx)) { LampOn[Idx] = On; }
		if (LampBright.IsValidIndex(Idx)) { LampBright[Idx] = Brightness01; }
	}
	else
	{
		LampIds.Add(LampId);
		LampOn.Add(On);
		LampBright.Add(Brightness01);
	}
}

bool UWorldSyncComponent::GetLampState(uint32 LampId, bool& bOutOn, float& OutBright) const
{
	const int32 Idx = LampIds.IndexOfByKey(LampId);
	if (Idx == INDEX_NONE || !LampOn.IsValidIndex(Idx) || !LampBright.IsValidIndex(Idx)) { return false; }
	bOutOn = LampOn[Idx] != 0;
	OutBright = LampBright[Idx];
	return true;
}

bool UWorldSyncComponent::GetSwitch(int32 Index, FVector& OutPos, float& OutYaw) const
{
	if (!SwitchPos.IsValidIndex(Index) || !SwitchYaw.IsValidIndex(Index)) { return false; }
	OutPos = SwitchPos[Index];
	OutYaw = SwitchYaw[Index];
	return true;
}

bool UWorldSyncComponent::HasSwitchNear(const FVector& Pos, float MaxDist) const
{
	const float D2 = MaxDist * MaxDist;
	for (const FVector& P : SwitchPos)
	{
		if (FVector::DistSquared(P, Pos) <= D2) { return true; }
	}
	return false;
}

void UWorldSyncComponent::ServerAddSwitch(const FVector& Pos, float Yaw)
{
	if (GetOwnerRole() != ROLE_Authority) { return; }
	// Dedupe op ~10cm: dezelfde schakelaar wordt via meerdere paden gepubliceerd (direct bij plaatsen/load
	// + de periodieke reconcile-pass in de DoorRetrofitter) - alleen de eerste telt.
	if (HasSwitchNear(Pos, 10.f)) { return; }
	SwitchPos.Add(Pos);
	SwitchYaw.Add(Yaw);
}

void UWorldSyncComponent::ServerRemoveSwitch(const FVector& Pos)
{
	if (GetOwnerRole() != ROLE_Authority) { return; }
	// De dichtstbijzijnde entry binnen 50cm verwijderen (de posities komen 1-op-1 van de server-actoren).
	int32 Best = INDEX_NONE;
	float BestD2 = 50.f * 50.f;
	for (int32 i = 0; i < SwitchPos.Num(); ++i)
	{
		const float D2 = FVector::DistSquared(SwitchPos[i], Pos);
		if (D2 <= BestD2) { BestD2 = D2; Best = i; }
	}
	if (Best != INDEX_NONE)
	{
		SwitchPos.RemoveAt(Best);
		if (SwitchYaw.IsValidIndex(Best)) { SwitchYaw.RemoveAt(Best); }
	}
}

void UWorldSyncComponent::ServerMarkSwitchesPublished()
{
	if (GetOwnerRole() != ROLE_Authority) { return; }
	bSwitchesPublished = 1;
}
