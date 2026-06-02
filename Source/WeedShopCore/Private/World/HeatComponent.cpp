#include "World/HeatComponent.h"
#include "UI/WeedToast.h"

#include "WeedShopCore.h"
#include "Game/WeedShopGameState.h"
#include "World/DayCycleComponent.h"
#include "Economy/EconomyComponent.h"
#include "Progression/UpgradeComponent.h"
#include "Engine/Engine.h"
#include "Net/UnrealNetwork.h"

UHeatComponent::UHeatComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	SetIsReplicatedByDefault(true);
}

void UHeatComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UHeatComponent, Heat);
}

float UHeatComponent::GetSecurityResist() const
{
	if (const AWeedShopGameState* GS = Cast<AWeedShopGameState>(GetOwner()))
	{
		if (const UUpgradeComponent* Upg = GS->GetUpgrades())
		{
			return FMath::Clamp(Upg->GetEffectTotal(TEXT("HeatResist")), 0.f, 0.9f);
		}
	}
	return 0.f;
}

void UHeatComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (GetOwnerRole() != ROLE_Authority)
	{
		return;
	}

	const AWeedShopGameState* GS = Cast<AWeedShopGameState>(GetOwner());
	const UDayCycleComponent* Day = GS ? GS->GetDayCycle() : nullptr;
	const bool bNight = Day && Day->IsNight();
	const float Resist = GetSecurityResist();

	// Heat zakt ALTIJD vanzelf (geen passieve opbouw meer). 's Nachts iets langzamer.
	const float Decay = bNight ? NightDecayPerSecond : DayDecayPerSecond;
	if (Heat > 0.f) { SetHeat(Heat - Decay * DeltaTime); }

	// Risico-events: alleen 's nachts, bij echt hoge heat, EN niet binnen de dagen-cooldown na een
	// vorige bust/overval (zodat je een paar dagen rust hebt).
	const int32 CurDay = Day ? Day->GetDayNumber() : 0;
	const bool bOnCooldown = (CurDay < LastEventDay + EventCooldownDays);
	if (bNight && Heat >= BustThreshold && !bOnCooldown)
	{
		EventTimer += DeltaTime;
		if (EventTimer >= EventIntervalSeconds)
		{
			EventTimer = 0.f;
			const float Chance = EventChance * (1.f - Resist);
			const float Roll = FMath::FRand();
			if (Roll < Chance * 0.4f)       { TriggerBust();    LastEventDay = CurDay; }
			else if (Roll < Chance)         { TriggerRobbery(); LastEventDay = CurDay; }
		}
	}
	else
	{
		EventTimer = 0.f;
	}
}

void UHeatComponent::AddHeat(float Amount)
{
	if (GetOwnerRole() != ROLE_Authority || Amount <= 0.f)
	{
		return;
	}
	SetHeat(Heat + Amount * (1.f - GetSecurityResist()));
}

void UHeatComponent::SetHeat(float NewHeat)
{
	Heat = FMath::Clamp(NewHeat, 0.f, 100.f);
	OnHeatChanged.Broadcast(Heat);
}

void UHeatComponent::OnRep_Heat()
{
	OnHeatChanged.Broadcast(Heat);
}

void UHeatComponent::TriggerBust()
{
	AWeedShopGameState* GS = Cast<AWeedShopGameState>(GetOwner());
	UEconomyComponent* Econ = GS ? GS->GetEconomy() : nullptr;
	if (!Econ)
	{
		return;
	}
	const int64 Loss = FMath::Max<int64>(500, (int64)(Econ->GetBalanceCents() * 0.2));
	Econ->RemoveMoney(Loss);
	SetHeat(Heat - 40.f);
	UE_LOG(LogWeedShop, Log, TEXT("BUST! Politie pakte %lld cents."), (long long)Loss);
	if (GEngine)
	{
		UWeedToast::Notify(-1, 6.f, FColor::Red,
			FString::Printf(TEXT("BUST! Police took EUR %.2f"), Loss / 100.f));
	}
}

void UHeatComponent::TriggerRobbery()
{
	AWeedShopGameState* GS = Cast<AWeedShopGameState>(GetOwner());
	UEconomyComponent* Econ = GS ? GS->GetEconomy() : nullptr;
	if (!Econ)
	{
		return;
	}
	const int64 Loss = FMath::Max<int64>(300, (int64)(Econ->GetBalanceCents() * 0.15));
	Econ->RemoveMoney(Loss);
	SetHeat(Heat - 15.f);
	UE_LOG(LogWeedShop, Log, TEXT("Overval! %lld cents gestolen."), (long long)Loss);
	if (GEngine)
	{
		UWeedToast::Notify(-1, 6.f, FColor(255, 140, 0),
			FString::Printf(TEXT("Robbery! EUR %.2f stolen"), Loss / 100.f));
	}
}
