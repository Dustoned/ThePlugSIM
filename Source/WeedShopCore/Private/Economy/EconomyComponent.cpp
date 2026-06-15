#include "Economy/EconomyComponent.h"
#include "UI/WeedToast.h"

#include "WeedShopCore.h"
#include "Game/WeedShopGameState.h"
#include "World/HeatComponent.h"
#include "World/DayCycleComponent.h"
#include "World/WorldItemPickup.h"
#include "GameFramework/Pawn.h"
#include "Engine/World.h"
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
	DOREPLIFETIME(UEconomyComponent, SafeCents);
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

UEconomyComponent* UEconomyComponent::BankOwner() const
{
	// Co-op: alle spelers delen één crew-bank (de GameState-economy). Competitive: ieder z'n eigen bank.
	const AWeedShopGameState* GS = GetWorld() ? GetWorld()->GetGameState<AWeedShopGameState>() : nullptr;
	if (GS && !GS->IsCompetitive())
	{
		if (UEconomyComponent* Shared = GS->GetSharedEconomy())
		{
			if (Shared != this) { return Shared; }
		}
	}
	return const_cast<UEconomyComponent*>(this);
}

int64 UEconomyComponent::GetBankCents() const
{
	return BankOwner()->BankCents;
}

void UEconomyComponent::SetBank(int64 NewCents)
{
	UEconomyComponent* Owner = BankOwner();
	Owner->BankCents = FMath::Max<int64>(0, NewCents);
	Owner->OnBalanceChanged.Broadcast(Owner->BalanceCents); // owner (gedeelde bank) prikkelen
	if (Owner != this) { OnBalanceChanged.Broadcast(BalanceCents); } // + lokale UI van deze speler
}

void UEconomyComponent::ChargeBank(int64 AmountCents)
{
	if (GetOwnerRole() != ROLE_Authority || AmountCents == 0) { return; }
	UEconomyComponent* Owner = BankOwner();
	Owner->BankCents -= AmountCents; // mag in de min: huur/schuld
	Owner->OnBalanceChanged.Broadcast(Owner->BalanceCents);
	if (Owner != this) { OnBalanceChanged.Broadcast(BalanceCents); }
}

void UEconomyComponent::OnRep_Balance()
{
	OnBalanceChanged.Broadcast(BalanceCents);
}

// === Bank (wit) ===

void UEconomyComponent::AddBank(int64 AmountCents, bool bTaxed)
{
	if (GetOwnerRole() != ROLE_Authority || AmountCents <= 0) { return; }
	const int64 Tax = bTaxed ? WeedRoundEuros((int64)FMath::RoundToDouble(AmountCents * DepositTaxPct)) : 0;
	SetBank(GetBankCents() + (AmountCents - Tax));
	OnMoneyEarned.Broadcast(AmountCents - Tax);
}

bool UEconomyComponent::RemoveBank(int64 AmountCents)
{
	if (GetOwnerRole() != ROLE_Authority) { return false; }
	if (AmountCents <= 0 || GetBankCents() < AmountCents) { return false; }
	SetBank(GetBankCents() - AmountCents);
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
	const int64 Fee = WeedRoundEuros((int64)FMath::RoundToDouble(AmountCents * TransferFeePct));
	// Per-speler geld: het bedrag + fee verlaat MIJN bank; de ontvanger wordt elders bijgeschreven.
	if (GetBankCents() < AmountCents + Fee)
	{
		if (GEngine) { UWeedToast::NotifyPawn(GetOwner(),-1, 3.f, FColor::Red, TEXT("Not enough bank money for that transfer.")); }
		return false;
	}
	RemoveBank(AmountCents + Fee);
	++TransfersToday;
	if (GEngine)
	{
		UWeedToast::NotifyPawn(GetOwner(),-1, 3.f, FColor(120, 200, 255),
			FString::Printf(TEXT("Sent EUR %lld to a friend (fee EUR %lld). Transfers left today: %d"),
				(long long)(WeedRoundEuros(AmountCents) / 100), (long long)(Fee / 100), GetTransfersRemainingToday()));
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
	const int64 Tax = WeedRoundEuros((int64)FMath::RoundToDouble(Amount * DepositTaxPct));
	const int64 ToBank = Amount - Tax;

	SetBalance(BalanceCents - Amount);  // cash eraf (lokaal/per speler)
	SetBank(GetBankCents() + ToBank);   // bank erbij (na belasting) - in co-op naar de gedeelde crew-bank
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
			FString::Printf(TEXT("Laundered EUR %lld -> bank (tax EUR %lld)"), (long long)(WeedRoundEuros(ToBank) / 100), (long long)(Tax / 100)));
	}
	return ToBank;
}

int64 UEconomyComponent::DepositToSafe(int64 CashCents)
{
	if (GetOwnerRole() != ROLE_Authority) { return 0; }
	const int64 Amt = FMath::Clamp<int64>(CashCents, 0, BalanceCents);
	if (Amt <= 0 || !RemoveMoney(Amt)) { return 0; } // cash eraf (untracked richting kluis)
	SafeCents += Amt;
	OnRep_Balance();
	return Amt;
}

int64 UEconomyComponent::WithdrawFromSafe(int64 SafeAmountCents)
{
	if (GetOwnerRole() != ROLE_Authority) { return 0; }
	const int64 Amt = FMath::Clamp<int64>(SafeAmountCents, 0, SafeCents);
	if (Amt <= 0) { return 0; }
	SafeCents -= Amt;
	AddMoneyUntracked(Amt); // terug naar cash (eigen geld, geen witwas-omzet)
	OnRep_Balance();
	return Amt;
}

void UEconomyComponent::SetSafeCents(int64 NewCents)
{
	SafeCents = FMath::Max<int64>(0, NewCents);
	OnRep_Balance();
}

void UEconomyComponent::ServerDropCash_Implementation(int32 Euros)
{
	if (GetOwnerRole() != ROLE_Authority || Euros <= 0) { return; }
	const int64 Cents = (int64)Euros * 100;
	if (BalanceCents < Cents) { return; }        // niet genoeg cash
	if (!RemoveMoney(Cents)) { return; }

	APawn* P = Cast<APawn>(GetOwner());
	UWorld* W = GetWorld();
	if (!P || !W) { return; }
	FVector Fwd = P->GetActorForwardVector(); Fwd.Z = 0.f; Fwd = Fwd.GetSafeNormal();
	FVector Loc = P->GetActorLocation() + Fwd * 90.f;
	Loc.Z -= (P->GetSimpleCollisionHalfHeight() - 12.f); // bij de voeten
	FActorSpawnParameters SP; SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	if (AWorldItemPickup* Pick = W->SpawnActor<AWorldItemPickup>(AWorldItemPickup::StaticClass(), FTransform(FRotator::ZeroRotator, Loc), SP))
	{
		Pick->Setup(FName(TEXT("Cash")), Euros, 0.f, 0.f); // Qty = euro's
	}
}
