#include "Economy/EconomyComponent.h"
#include "UI/WeedToast.h"

#include "WeedShopCore.h"
#include "Game/WeedShopGameState.h"
#include "World/HeatComponent.h"
#include "World/DayCycleComponent.h"
#include "Engine/Engine.h"
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
		SetBalance(StartingBalanceCents);   // cash (zwart)
		SetBank(StartingBankCents);         // bank (wit) - demo: ook startgeld zodat de winkel meteen werkt
	}
}

void UEconomyComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UEconomyComponent, BalanceCents);
	DOREPLIFETIME(UEconomyComponent, BankCents);
	DOREPLIFETIME(UEconomyComponent, DepositedTodayCents);
	DOREPLIFETIME(UEconomyComponent, LegitIncomeCents);
	DOREPLIFETIME(UEconomyComponent, LaunderedCents);
	DOREPLIFETIME(UEconomyComponent, TransfersToday);
}

void UEconomyComponent::AddMoney(int64 AmountCents)
{
	if (GetOwnerRole() != ROLE_Authority)
	{
		UE_LOG(LogWeedShop, Warning, TEXT("AddMoney ignored: only the server may mutate the balance."));
		return;
	}
	if (AmountCents <= 0)
	{
		return;
	}
	SetBalance(BalanceCents + AmountCents);
	OnMoneyEarned.Broadcast(AmountCents);
}

void UEconomyComponent::AddMoneyUntracked(int64 AmountCents)
{
	if (GetOwnerRole() != ROLE_Authority)
	{
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
		UE_LOG(LogWeedShop, Warning, TEXT("RemoveMoney ignored: only the server may mutate the balance."));
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

void UEconomyComponent::SetBankCents(int64 NewCents)
{
	if (GetOwnerRole() != ROLE_Authority) { return; }
	SetBank(FMath::Max<int64>(0, NewCents));
}

void UEconomyComponent::SetBalance(int64 NewCents)
{
	BalanceCents = NewCents;

	// OnRep draait niet op de authority; vuur de delegate hier zodat host-UI ook meekrijgt.
	OnBalanceChanged.Broadcast(BalanceCents);
}

void UEconomyComponent::SetBank(int64 NewCents)
{
	BankCents = FMath::Max<int64>(0, NewCents);
	OnBalanceChanged.Broadcast(BalanceCents);
}

void UEconomyComponent::ChargeBank(int64 AmountCents)
{
	if (GetOwnerRole() != ROLE_Authority || AmountCents == 0) { return; }
	BankCents -= AmountCents; // mag in de min: huur/schuld
	OnBalanceChanged.Broadcast(BalanceCents);
}

void UEconomyComponent::OnRep_Balance()
{
	OnBalanceChanged.Broadcast(BalanceCents);
}

// === Bank (wit) ===

void UEconomyComponent::AddBank(int64 AmountCents, bool bTaxed)
{
	if (GetOwnerRole() != ROLE_Authority || AmountCents <= 0) { return; }
	const int64 Tax = bTaxed ? (int64)FMath::RoundToDouble(AmountCents * DepositTaxPct) : 0;
	SetBank(BankCents + (AmountCents - Tax));
	OnMoneyEarned.Broadcast(AmountCents - Tax);
}

bool UEconomyComponent::RemoveBank(int64 AmountCents)
{
	if (GetOwnerRole() != ROLE_Authority) { return false; }
	if (AmountCents <= 0 || BankCents < AmountCents) { return false; }
	SetBank(BankCents - AmountCents);
	return true;
}

void UEconomyComponent::RefreshDepositDay()
{
	// De portemonnee zit op de pawn; de gedeelde dag-klok staat op de GameState.
	const AWeedShopGameState* GS = GetWorld() ? GetWorld()->GetGameState<AWeedShopGameState>() : nullptr;
	const int32 Today = (GS && GS->GetDayCycle()) ? GS->GetDayCycle()->GetDayNumber() : 0;
	if (Today != DepositDay)
	{
		DepositDay = Today;
		DepositedTodayCents = 0;
		TransfersToday = 0; // ook de overboek-teller reset per dag
	}
}

void UEconomyComponent::NoteLegitIncome(int64 Cents)
{
	if (GetOwnerRole() != ROLE_Authority || Cents <= 0) { return; }
	LegitIncomeCents += Cents; // verkoop-omzet = "schone ruimte" om wit te wassen zonder heat
}

bool UEconomyComponent::TransferBank(int64 AmountCents)
{
	if (GetOwnerRole() != ROLE_Authority || AmountCents <= 0) { return false; }
	RefreshDepositDay();
	if (GetTransfersRemainingToday() <= 0)
	{
		if (GEngine) { UWeedToast::NotifyPawn(GetOwner(),-1, 3.f, FColor::Orange, TEXT("Daily transfer limit reached - try again tomorrow.")); }
		return false;
	}
	const int64 Fee = (int64)FMath::RoundToDouble(AmountCents * TransferFeePct);
	// Per-speler geld: het bedrag + fee verlaat MIJN bank; de ontvanger wordt elders bijgeschreven.
	if (BankCents < AmountCents + Fee)
	{
		if (GEngine) { UWeedToast::NotifyPawn(GetOwner(),-1, 3.f, FColor::Red, TEXT("Not enough bank money for that transfer.")); }
		return false;
	}
	RemoveBank(AmountCents + Fee);
	++TransfersToday;
	if (GEngine)
	{
		UWeedToast::NotifyPawn(GetOwner(),-1, 3.f, FColor(120, 200, 255),
			FString::Printf(TEXT("Sent EUR %.2f to a friend (fee EUR %.2f). Transfers left today: %d"),
				AmountCents / 100.f, Fee / 100.f, GetTransfersRemainingToday()));
	}
	return true;
}

int64 UEconomyComponent::Deposit(int64 CashAmount)
{
	if (GetOwnerRole() != ROLE_Authority || CashAmount <= 0) { return 0; }
	if (BalanceCents < CashAmount)
	{
		if (GEngine) { UWeedToast::NotifyPawn(GetOwner(),-1, 3.f, FColor::Red, TEXT("Not enough cash to deposit.")); }
		return 0;
	}
	RefreshDepositDay();
	const int64 Remaining = GetDailyDepositRemainingCents();
	if (Remaining <= 0)
	{
		if (GEngine) { UWeedToast::NotifyPawn(GetOwner(),-1, 3.f, FColor::Orange, TEXT("Daily laundering limit reached - come back tomorrow.")); }
		return 0;
	}
	const int64 Amount = FMath::Min(CashAmount, Remaining);
	const int64 Tax = (int64)FMath::RoundToDouble(Amount * DepositTaxPct);
	const int64 ToBank = Amount - Tax;

	SetBalance(BalanceCents - Amount);  // cash eraf
	SetBank(BankCents + ToBank);        // bank erbij (na belasting)
	DepositedTodayCents += Amount;

	// Heat: het deel dat je MET verkoop kunt verklaren is "schoon" (weinig heat); de rest is verdacht
	// (gehamsterd/rewards/transfers zonder shop-omzet) en geeft VEEL heat. Zo loont alleen-maar-storten niet.
	const int64 Headroom = FMath::Max<int64>(0, LegitIncomeCents - LaunderedCents);
	const int64 Clean = FMath::Min(Amount, Headroom);
	const int64 Dirty = Amount - Clean;
	LaunderedCents += Amount;

	float HeatAdd = (float)Clean / 100000.f * CleanDepositHeatPer1000
		+ (float)Dirty / 100000.f * DirtyDepositHeatPer1000;
	// Grote dag-dumps compounden: hoe meer je vandaag al stortte, hoe verdachter elke euro erbovenop.
	HeatAdd *= 1.f + (float)DepositedTodayCents / 100000.f * DailyDumpHeatRamp;

	if (AWeedShopGameState* GS = GetWorld() ? GetWorld()->GetGameState<AWeedShopGameState>() : nullptr)
	{
		if (UHeatComponent* Heat = GS->GetHeat())
		{
			Heat->AddHeat(HeatAdd);
		}
	}
	if (GEngine)
	{
		UWeedToast::NotifyPawn(GetOwner(),-1, 3.f, FColor(120, 200, 255),
			FString::Printf(TEXT("Laundered EUR %.2f -> bank (tax EUR %.2f)"), ToBank / 100.f, Tax / 100.f));
	}
	return ToBank;
}
