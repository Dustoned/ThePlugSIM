#include "Game/WeedShopGameMode.h"

#include "Game/WeedShopGameState.h"

AWeedShopGameMode::AWeedShopGameMode()
{
	GameStateClass = AWeedShopGameState::StaticClass();
}
