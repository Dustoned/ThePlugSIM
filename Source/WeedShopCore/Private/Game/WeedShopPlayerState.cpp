#include "Game/WeedShopPlayerState.h"

#include "Net/UnrealNetwork.h"

void AWeedShopPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AWeedShopPlayerState, PlugPid);
}
