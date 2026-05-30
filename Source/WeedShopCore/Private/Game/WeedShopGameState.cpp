#include "Game/WeedShopGameState.h"

#include "Economy/EconomyComponent.h"
#include "World/DayCycleComponent.h"
#include "Progression/MilestoneComponent.h"
#include "Progression/UpgradeComponent.h"
#include "Progression/StoreComponent.h"
#include "Phone/ContactsComponent.h"

AWeedShopGameState::AWeedShopGameState()
{
	// Gedeelde, replicerende subobjects op de GameState.
	Economy = CreateDefaultSubobject<UEconomyComponent>(TEXT("Economy"));
	DayCycle = CreateDefaultSubobject<UDayCycleComponent>(TEXT("DayCycle"));
	Milestones = CreateDefaultSubobject<UMilestoneComponent>(TEXT("Milestones"));
	Upgrades = CreateDefaultSubobject<UUpgradeComponent>(TEXT("Upgrades"));
	Store = CreateDefaultSubobject<UStoreComponent>(TEXT("Store"));
	Contacts = CreateDefaultSubobject<UContactsComponent>(TEXT("Contacts"));
}
