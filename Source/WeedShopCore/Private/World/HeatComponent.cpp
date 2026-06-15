#include "World/HeatComponent.h"
#include "UI/WeedToast.h"

#include "WeedShopCore.h"
#include "Game/WeedShopGameState.h"
#include "World/DayCycleComponent.h"
#include "Economy/EconomyComponent.h"
#include "Progression/UpgradeComponent.h"
#include "Phone/PhoneClientComponent.h"
#include "Cultivation/GrowPlant.h"
#include "Cultivation/DryingRack.h"
#include "World/ProcessorMachine.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "EngineUtils.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"

namespace
{
	// Co-op: stuur een melding naar ALLE spelers (elk op z'n eigen client) i.p.v. alleen lokaal op de host.
	// NotifyPawn routeert per pawn via de PhoneClientComponent naar de juiste client. Valt terug op een
	// lokale melding als er (nog) geen pawns zijn.
	void NotifyAllPlayers(UWorld* W, const FColor& Color, float Time, const FString& Msg)
	{
		bool bAny = false;
		if (W)
		{
			for (FConstPlayerControllerIterator It = W->GetPlayerControllerIterator(); It; ++It)
			{
				if (APawn* Pw = It->Get() ? It->Get()->GetPawn() : nullptr)
				{
					UWeedToast::NotifyPawn(Pw, -1, Time, Color, Msg);
					bAny = true;
				}
			}
		}
		if (!bAny) { UWeedToast::Notify(-1, Time, Color, Msg); }
	}
}

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
	const int64 Loss = FMath::Max<int64>(500, WeedRoundEuros((int64)(Econ->GetBalanceCents() * 0.2)));
	Econ->RemoveMoney(Loss);
	SetHeat(Heat - 40.f);
	UE_LOG(LogWeedShop, Log, TEXT("BUST! Politie pakte %lld cents."), (long long)Loss);
	NotifyAllPlayers(GetWorld(), FColor::Red, 6.f,
		FString::Printf(TEXT("BUST! Police took EUR %lld"), (long long)(WeedRoundEuros(Loss) / 100)));
}

void UHeatComponent::TriggerRobbery()
{
	AWeedShopGameState* GS = Cast<AWeedShopGameState>(GetOwner());
	UEconomyComponent* Econ = GS ? GS->GetEconomy() : nullptr;
	if (!Econ)
	{
		return;
	}
	const int64 Loss = FMath::Max<int64>(300, WeedRoundEuros((int64)(Econ->GetBalanceCents() * 0.15)));
	Econ->RemoveMoney(Loss);   // alleen on-hand cash: de kluis (SafeCents) en de bank blijven veilig
	SetHeat(Heat - 15.f);

	// Overvallers halen ook je ACTIEVE apartment leeg: alle groeiende planten, droogrekken en machines.
	int32 Plants = 0, Racks = 0, Machines = 0;
	if (UWorld* World = GetWorld())
	{
		FVector HomePos = FVector::ZeroVector; bool bHasHome = false;
		if (APlayerController* PC = World->GetFirstPlayerController())
		{
			if (APawn* Pw = PC->GetPawn())
			{
				if (UPhoneClientComponent* Ph = Pw->FindComponentByClass<UPhoneClientComponent>())
				{
					bHasHome = Ph->GetActiveHomeLocation(HomePos);
				}
			}
		}
		if (bHasHome)
		{
			const float R2 = 1600.f * 1600.f; // radius rond het apartment-interieur (~16m, dekt een kamer)
			for (TActorIterator<AGrowPlant> It(World); It; ++It)
			{ if (FVector::DistSquared(It->GetActorLocation(), HomePos) < R2) { It->RobClear(); ++Plants; } }
			for (TActorIterator<ADryingRack> It(World); It; ++It)
			{ if (FVector::DistSquared(It->GetActorLocation(), HomePos) < R2) { It->RestoreEntries(TArray<FDryEntry>()); ++Racks; } }
			for (TActorIterator<AProcessorMachine> It(World); It; ++It)
			{ if (FVector::DistSquared(It->GetActorLocation(), HomePos) < R2) { It->RestoreEntries(TArray<FProcEntry>()); ++Machines; } }
		}
	}

	UE_LOG(LogWeedShop, Log, TEXT("Overval! %lld cents + apartment leeggehaald (%d planten, %d rekken, %d machines)."),
		(long long)Loss, Plants, Racks, Machines);
	NotifyAllPlayers(GetWorld(), FColor(255, 140, 0), 8.f,
		FString::Printf(TEXT("Robbery! EUR %lld stolen + your apartment got cleaned out (%d plants, %d racks, %d machines). Use a safe!"),
			(long long)(WeedRoundEuros(Loss) / 100), Plants, Racks, Machines));
}
