#include "Economy/EconomyComponent.h"

#include "WeedShopCore.h"
#include "Net/UnrealNetwork.h"

UEconomyComponent::UEconomyComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UEconomyComponent::BeginPlay()
{
	Super::BeginPlay();

	// Alleen de server zet het startsaldo; het repliceert daarna naar de clients.
	if (GetOwnerRole() == ROLE_Authority)
	{
		SetBalance(StartingBalanceCents);
	}
}

void UEconomyComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UEconomyComponent, BalanceCents);
}

void UEconomyComponent::AddMoney(int64 AmountCents)
{
	if (GetOwnerRole() != ROLE_Authority)
	{
		UE_LOG(LogWeedShop, Warning, TEXT("AddMoney genegeerd: alleen de server mag het saldo muteren."));
		return;
	}
	if (AmountCents <= 0)
	{
		return;
	}
	SetBalance(BalanceCents + AmountCents);
}

bool UEconomyComponent::RemoveMoney(int64 AmountCents)
{
	if (GetOwnerRole() != ROLE_Authority)
	{
		UE_LOG(LogWeedShop, Warning, TEXT("RemoveMoney genegeerd: alleen de server mag het saldo muteren."));
		return false;
	}
	if (AmountCents <= 0 || BalanceCents < AmountCents)
	{
		return false;
	}
	SetBalance(BalanceCents - AmountCents);
	return true;
}

void UEconomyComponent::SetBalanceCents(int64 NewCents)
{
	if (GetOwnerRole() != ROLE_Authority)
	{
		return;
	}
	SetBalance(FMath::Max<int64>(0, NewCents));
}

void UEconomyComponent::SetBalance(int64 NewCents)
{
	BalanceCents = NewCents;

	// OnRep draait niet op de authority; vuur de delegate hier zodat host-UI ook meekrijgt.
	OnBalanceChanged.Broadcast(BalanceCents);
}

void UEconomyComponent::OnRep_Balance()
{
	OnBalanceChanged.Broadcast(BalanceCents);
}
