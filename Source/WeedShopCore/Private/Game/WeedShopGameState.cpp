#include "Game/WeedShopGameState.h"

#include "Economy/EconomyComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "World/DayCycleComponent.h"
#include "Progression/MilestoneComponent.h"
#include "Progression/UpgradeComponent.h"
#include "Progression/StoreComponent.h"
#include "Phone/ContactsComponent.h"
#include "Npc/NpcRegistryComponent.h"
#include "World/HeatComponent.h"
#include "Progression/LevelComponent.h"

AWeedShopGameState::AWeedShopGameState()
{
	// Gedeelde, replicerende subobjects op de GameState.
	Economy = CreateDefaultSubobject<UEconomyComponent>(TEXT("Economy"));
	DayCycle = CreateDefaultSubobject<UDayCycleComponent>(TEXT("DayCycle"));
	Milestones = CreateDefaultSubobject<UMilestoneComponent>(TEXT("Milestones"));
	Upgrades = CreateDefaultSubobject<UUpgradeComponent>(TEXT("Upgrades"));
	Store = CreateDefaultSubobject<UStoreComponent>(TEXT("Store"));
	Contacts = CreateDefaultSubobject<UContactsComponent>(TEXT("Contacts"));
	NpcRegistry = CreateDefaultSubobject<UNpcRegistryComponent>(TEXT("NpcRegistry"));
	Heat = CreateDefaultSubobject<UHeatComponent>(TEXT("Heat"));
	Leveling = CreateDefaultSubobject<ULevelComponent>(TEXT("Leveling"));
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
