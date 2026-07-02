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

	// "Te veel potten": tel periodiek de potten rond je apartment; elke pot boven de cap houdt een
	// heat-VLOER aan (gedempt door beveiliging). Niet elke frame tellen - om de paar seconden.
	PotScanTimer += DeltaTime;
	if (PotScanTimer >= 3.f) { PotScanTimer = 0.f; CachedPotFloor = ComputePotHeatFloor() * (1.f - Resist); }

	// Heat zakt ALTIJD vanzelf ('s nachts iets langzamer), maar niet onder de pot-vloer.
	const float Decay = bNight ? NightDecayPerSecond : DayDecayPerSecond;
	const float NewHeat = FMath::Max(Heat - Decay * DeltaTime, CachedPotFloor);
	if (!FMath::IsNearlyEqual(NewHeat, Heat)) { SetHeat(NewHeat); }

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

float UHeatComponent::ComputePotHeatFloor()
{
	UWorld* World = GetWorld();
	if (!World) { return 0.f; }

	// Verzamel de thuis-plek van elke speler (co-op: meestal een gedeeld apartment; competitive: elk een eigen).
	TArray<FVector> Homes;
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		APawn* Pw = It->Get() ? It->Get()->GetPawn() : nullptr;
		UPhoneClientComponent* Ph = Pw ? Pw->FindComponentByClass<UPhoneClientComponent>() : nullptr;
		FVector HP;
		if (Ph && Ph->GetActiveHomeLocation(HP)) { Homes.AddUnique(HP); }
	}
	if (Homes.Num() == 0) { bWasOverPotCap = false; return 0.f; }

	// Tel de potten (AGrowPlant) binnen het apartment-bereik. De grote Fabric-pot is óók 1 AGrowPlant -> telt als 1.
	const float R2 = 1600.f * 1600.f; // ~16m rond de apartment-plek (zelfde radius als de overval)
	int32 PotCount = 0;
	for (TActorIterator<AGrowPlant> It(World); It; ++It)
	{
		const FVector Loc = It->GetActorLocation();
		for (const FVector& HP : Homes)
		{
			if (FVector::DistSquared(Loc, HP) < R2) { ++PotCount; break; }
		}
	}

	const int32 Cap = FMath::Max(1, PotCap);
	const int32 Excess = FMath::Max(0, PotCount - Cap);

	// Eenmalige waarschuwing zodra je over de cap gaat (niet elke scan spammen).
	const bool bOver = Excess > 0;
	if (bOver && !bWasOverPotCap)
	{
		NotifyAllPlayers(World, FColor(255, 140, 0), 6.f,
			FString::Printf(TEXT("Too many pots (%d/%d)! Extra plants keep police heat up - get a bigger place."), PotCount, Cap));
	}
	bWasOverPotCap = bOver;

	return FMath::Min((float)Excess * HeatPerExcessPot, MaxPotHeat);
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
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	SetHeat(Heat - 40.f);

	// Co-op: beboet ELKE speler apart op DIENS EIGEN cash-saldo. GS->GetEconomy() = altijd de host
	// (GetFirstPlayerController) -> joiner verloor anders nooit geld. Pak per pawn de eigen EconomyComponent
	// en trek daar de bust-straf (20%, floor EUR 5) af; meld het bedrag per speler op diens eigen client.
	UE_LOG(LogWeedShop, Log, TEXT("BUST! Politie pakt cash van elke speler."));
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		APawn* Pw = It->Get() ? It->Get()->GetPawn() : nullptr;
		UEconomyComponent* Econ = Pw ? Pw->FindComponentByClass<UEconomyComponent>() : nullptr;
		if (!Econ) { continue; }
		const int64 Loss = FMath::Max<int64>(500, WeedRoundEuros((int64)(Econ->GetBalanceCents() * 0.2)));
		Econ->RemoveMoney(Loss);
		UWeedToast::NotifyPawn(Pw, -1, 6.f, FColor::Red,
			FString::Printf(TEXT("BUST! Police took EUR %lld"), (long long)(WeedRoundEuros(Loss) / 100)));
	}
}

void UHeatComponent::TriggerRobbery()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	SetHeat(Heat - 15.f);

	// Co-op: berooft ELKE speler apart van diens EIGEN on-hand cash (15%, floor EUR 3). GS->GetEconomy() =
	// altijd de host -> joiner verloor anders nooit cash. De kluis (SafeCents) en de bank blijven veilig.
	// Melding per speler op diens eigen client met het eigen verloren bedrag.
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		APawn* Pw = It->Get() ? It->Get()->GetPawn() : nullptr;
		UEconomyComponent* Econ = Pw ? Pw->FindComponentByClass<UEconomyComponent>() : nullptr;
		if (!Econ) { continue; }
		const int64 Loss = FMath::Max<int64>(300, WeedRoundEuros((int64)(Econ->GetBalanceCents() * 0.15)));
		Econ->RemoveMoney(Loss);
		UWeedToast::NotifyPawn(Pw, -1, 8.f, FColor(255, 140, 0),
			FString::Printf(TEXT("Robbery! EUR %lld stolen from your cash. Use a safe!"),
				(long long)(WeedRoundEuros(Loss) / 100)));
	}

	// Overvallers halen ook je ACTIEVE apartment leeg: alle groeiende planten, droogrekken en machines.
	int32 Plants = 0, Racks = 0, Machines = 0;
	{
		// Verzamel de thuis-plek van ELKE speler (niet alleen de host): in co-op delen ze er meestal een,
		// in competitive hebben ze er elk een -> berooft het apartment van iedereen, niet alleen speler 0.
		TArray<FVector> Homes;
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			APawn* Pw = It->Get() ? It->Get()->GetPawn() : nullptr;
			UPhoneClientComponent* Ph = Pw ? Pw->FindComponentByClass<UPhoneClientComponent>() : nullptr;
			FVector HP;
			if (Ph && Ph->GetActiveHomeLocation(HP)) { Homes.AddUnique(HP); }
		}
		if (Homes.Num() > 0)
		{
			const float R2 = 1600.f * 1600.f; // radius rond het apartment-interieur (~16m, dekt een kamer)
			auto NearAnyHome = [&Homes, R2](const FVector& Loc)
			{
				for (const FVector& HP : Homes) { if (FVector::DistSquared(Loc, HP) < R2) { return true; } }
				return false;
			};
			for (TActorIterator<AGrowPlant> It(World); It; ++It)
			{ if (NearAnyHome(It->GetActorLocation())) { It->RobClear(); ++Plants; } }
			for (TActorIterator<ADryingRack> It(World); It; ++It)
			{ if (NearAnyHome(It->GetActorLocation())) { It->RestoreEntries(TArray<FDryEntry>()); ++Racks; } }
			for (TActorIterator<AProcessorMachine> It(World); It; ++It)
			{ if (NearAnyHome(It->GetActorLocation())) { It->RestoreEntries(TArray<FProcEntry>()); ++Machines; } }
		}
	}

	UE_LOG(LogWeedShop, Log, TEXT("Overval! Cash van elke speler + apartment leeggehaald (%d planten, %d rekken, %d machines)."),
		Plants, Racks, Machines);
	if (Plants > 0 || Racks > 0 || Machines > 0)
	{
		NotifyAllPlayers(World, FColor(255, 140, 0), 8.f,
			FString::Printf(TEXT("Robbery! Your apartment got cleaned out (%d plants, %d racks, %d machines)."),
				Plants, Racks, Machines));
	}
}
