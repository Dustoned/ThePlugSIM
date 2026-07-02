#include "Game/WeedShopGameState.h"

#include "WeedShopCore.h"
#include "Economy/EconomyComponent.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "TimerManager.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/Pawn.h"
#include "Save/SaveGameSubsystem.h"
#include "Net/UnrealNetwork.h"
#include "World/DayCycleComponent.h"
#include "Progression/MilestoneComponent.h"
#include "Progression/GoalsComponent.h"
#include "Progression/UpgradeComponent.h"
#include "Progression/StoreComponent.h"
#include "Phone/ContactsComponent.h"
#include "World/WorldSyncComponent.h"
#include "Npc/NpcRegistryComponent.h"
#include "World/HeatComponent.h"
#include "Progression/LevelComponent.h"

AWeedShopGameState::AWeedShopGameState()
{
	// Gedeelde, replicerende subobjects op de GameState.
	Economy = CreateDefaultSubobject<UEconomyComponent>(TEXT("Economy"));
	DayCycle = CreateDefaultSubobject<UDayCycleComponent>(TEXT("DayCycle"));
	Milestones = CreateDefaultSubobject<UMilestoneComponent>(TEXT("Milestones"));
	Goals = CreateDefaultSubobject<UGoalsComponent>(TEXT("Goals"));
	Upgrades = CreateDefaultSubobject<UUpgradeComponent>(TEXT("Upgrades"));
	Store = CreateDefaultSubobject<UStoreComponent>(TEXT("Store"));
	Contacts = CreateDefaultSubobject<UContactsComponent>(TEXT("Contacts"));
	WorldSync = CreateDefaultSubobject<UWorldSyncComponent>(TEXT("WorldSync"));
	NpcRegistry = CreateDefaultSubobject<UNpcRegistryComponent>(TEXT("NpcRegistry"));
	Heat = CreateDefaultSubobject<UHeatComponent>(TEXT("Heat"));
	Leveling = CreateDefaultSubobject<ULevelComponent>(TEXT("Leveling"));
}

void AWeedShopGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AWeedShopGameState, SaveCounter);
	DOREPLIFETIME(AWeedShopGameState, LoadCounter);
	DOREPLIFETIME(AWeedShopGameState, bFreeBuild);
	DOREPLIFETIME(AWeedShopGameState, bDevTools);
	DOREPLIFETIME(AWeedShopGameState, CoopMode);
	DOREPLIFETIME(AWeedShopGameState, Standings);
	DOREPLIFETIME(AWeedShopGameState, ActiveDeliveries);
}

void AWeedShopGameState::AddDeliveryTarget(int32 OrderId, const FVector& World)
{
	if (!HasAuthority()) { return; }
	for (FActiveDelivery& D : ActiveDeliveries) { if (D.OrderId == OrderId) { D.World = World; return; } }
	FActiveDelivery D; D.OrderId = OrderId; D.World = World; ActiveDeliveries.Add(D);
}

void AWeedShopGameState::RemoveDeliveryTarget(int32 OrderId)
{
	if (!HasAuthority()) { return; }
	ActiveDeliveries.RemoveAll([OrderId](const FActiveDelivery& D) { return D.OrderId == OrderId; });
}

void AWeedShopGameState::BeginPlay()
{
	// MEET-MARKER (boot-gap-diagnose, B.15): wanneer de wereld-state echt tot leven komt t.o.v. de
	// proces-start - samen met de GameMode-ctor- en NpcRegistry-markers geeft dit de gap-verdeling.
	UE_LOG(LogWeedShop, Display, TEXT("[BOOTMARK] GameState::BeginPlay (+%.2fs sinds start)"), FPlatformTime::Seconds() - GStartTime);
	Super::BeginPlay();
	// Alleen de host/server autosavet de gedeelde + alle-spelers-staat, elke AutoSaveSeconds.
	if (HasAuthority() && AutoSaveSeconds > 0.f && GetWorld())
	{
		GetWorldTimerManager().SetTimer(AutoSaveTimer, this, &AWeedShopGameState::AutoSave, AutoSaveSeconds, true);
	}
	// Competitive scorebord: herbereken elke 3s op de server (repliceert de standen naar alle spelers).
	if (HasAuthority() && GetWorld())
	{
		GetWorldTimerManager().SetTimer(StandingsTimer, this, &AWeedShopGameState::UpdateStandings, 3.f, true);
	}
}

void AWeedShopGameState::UpdateStandings()
{
	if (!HasAuthority() || !GetWorld()) { return; }
	if (CoopMode != ECoopMode::Competitive) { if (Standings.Num() > 0) { Standings.Reset(); } return; }

	TArray<FCompetitorScore> New;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		APawn* P = PC ? PC->GetPawn() : nullptr;
		if (!P) { continue; }
		UEconomyComponent* E = P->FindComponentByClass<UEconomyComponent>();
		if (!E) { continue; }
		FCompetitorScore S;
		S.Name = (PC->PlayerState && !PC->PlayerState->GetPlayerName().IsEmpty())
			? PC->PlayerState->GetPlayerName() : FString::Printf(TEXT("Player %d"), New.Num() + 1);
		S.CashCents = E->GetCashCents();
		S.BankCents = E->GetBankCents();
		S.NetWorthCents = S.CashCents + S.BankCents;
		S.EarnedCents = E->GetLegitIncomeCents();
		if (NpcRegistry) { S.Customers = NpcRegistry->CountPlayerCustomers(USaveGameSubsystem::StablePlayerId(P)); }
		New.Add(S);
	}
	New.Sort([](const FCompetitorScore& A, const FCompetitorScore& B) { return A.NetWorthCents > B.NetWorthCents; });
	Standings = New;
}

void AWeedShopGameState::AutoSave()
{
	if (UGameInstance* GI = GetGameInstance())
	{
		if (USaveGameSubsystem* Sv = GI->GetSubsystem<USaveGameSubsystem>())
		{
			if (Sv->IsAutosaveEnabled())
			{
				Sv->SaveGame(true); // autosave -> apart bestand, overschrijft je echte save niet
			}
		}
	}
}

UEconomyComponent* AWeedShopGameState::GetEconomy() const
{
	// Portemonnee per speler (op de pawn). Voor lokale UI/HUD/heat/save geven we de portemonnee
	// van de lokale speler terug; vóór er een pawn is valt het terug op de GameState-kas.
	if (const UWorld* W = GetWorld())
	{
		if (const APlayerController* PC = W->GetFirstPlayerController())
		{
			if (const APawn* P = PC->GetPawn())
			{
				if (UEconomyComponent* E = P->FindComponentByClass<UEconomyComponent>())
				{
					return E;
				}
			}
		}
	}
	return Economy;
}
