#include "Game/WeedShopGameState.h"

#include "Economy/EconomyComponent.h"
#include "World/DayCycleComponent.h"

AWeedShopGameState::AWeedShopGameState()
{
	// Gedeelde, replicerende subobjects op de GameState.
	Economy = CreateDefaultSubobject<UEconomyComponent>(TEXT("Economy"));
	DayCycle = CreateDefaultSubobject<UDayCycleComponent>(TEXT("DayCycle"));
}
