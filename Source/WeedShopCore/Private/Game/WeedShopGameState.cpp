#include "Game/WeedShopGameState.h"

#include "Economy/EconomyComponent.h"

AWeedShopGameState::AWeedShopGameState()
{
	// De kas als default-subobject: bestaat altijd op de GameState en repliceert mee.
	Economy = CreateDefaultSubobject<UEconomyComponent>(TEXT("Economy"));
}
