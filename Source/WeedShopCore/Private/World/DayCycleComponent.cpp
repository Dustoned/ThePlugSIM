#include "World/DayCycleComponent.h"

#include "Net/UnrealNetwork.h"

UDayCycleComponent::UDayCycleComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	SetIsReplicatedByDefault(true);
}

void UDayCycleComponent::BeginPlay()
{
	Super::BeginPlay();
	bWasNight = IsNight();
}

void UDayCycleComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UDayCycleComponent, TimeOfDaySeconds);
	DOREPLIFETIME(UDayCycleComponent, DayNumber);
}

void UDayCycleComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// Alleen de server telt de tijd op; clients krijgen TimeOfDaySeconds via replicatie.
	if (GetOwnerRole() != ROLE_Authority)
	{
		return;
	}

	TimeOfDaySeconds += DeltaTime;
	if (TimeOfDaySeconds >= CycleLength())
	{
		TimeOfDaySeconds -= CycleLength();
		++DayNumber; // nieuwe dag
	}

	CheckTransition();
}

float UDayCycleComponent::GetCycleFraction() const
{
	return TimeOfDaySeconds / CycleLength();
}

void UDayCycleComponent::SetTimeOfDaySeconds(float NewTime)
{
	if (GetOwnerRole() != ROLE_Authority)
	{
		return;
	}
	TimeOfDaySeconds = FMath::Fmod(FMath::Max(0.f, NewTime), CycleLength());
	CheckTransition();
}

void UDayCycleComponent::OnRep_Time()
{
	CheckTransition();
}

void UDayCycleComponent::CheckTransition()
{
	const bool bNow = IsNight();
	if (bNow != bWasNight)
	{
		bWasNight = bNow;
		OnDayNightChanged.Broadcast(bNow);
	}
}
