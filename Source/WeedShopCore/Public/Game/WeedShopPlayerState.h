// AWeedShopPlayerState — draagt de stabiele per-speler-id (PlugPid) voor alle per-speler keys
// (competitive level/heat-entries, per-speler save-records). Zonder Online Subsystem is de
// UniqueNetId ongeldig en viel StablePlayerId terug op de spelernaam ("Player") -> stille
// key-botsing bij 2 gelijke namen (per-speler-state deelde dan 1 entry, raakt ook de lokale
// 2-instance-test). PlugPid = login-id van de machine (joiner stuurt 'm mee op de join-URL,
// host pakt z'n eigen login-id), gededupet met een "#2"-suffix in AWeedShopGameMode::InitNewPlayer.
// Repliceert zodat host EN clients dezelfde sleutel resolven.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "WeedShopPlayerState.generated.h"

UCLASS()
class WEEDSHOPCORE_API AWeedShopPlayerState : public APlayerState
{
	GENERATED_BODY()

public:
	// Stabiele speler-id (zie boven). Leeg = (nog) niet gezet -> StablePlayerId valt terug op
	// UniqueNetId en daarna op de spelernaam (migratie-vriendelijk voor oude records).
	UPROPERTY(Replicated)
	FString PlugPid;

protected:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
};
