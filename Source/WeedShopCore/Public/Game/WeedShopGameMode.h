// AWeedShopGameMode — zet de juiste (replicerende) GameState-class zodat de gedeelde co-op-kas
// altijd bestaat. Pawn/PlayerController/HUD laat je in een BP-subclass staan (of via reparent
// van je bestaande GameMode-BP), zodat de First-Person-pawn behouden blijft.
//
// Editor-koppeling (kies één):
//  A) Reparent BP_FirstPersonGameMode -> AWeedShopGameMode (behoudt de pawn-instellingen).
//  B) Of laat je GameMode zoals hij is en zet alleen "Game State Class" = BP_WeedShopGameState.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "WeedShopGameMode.generated.h"

UCLASS()
class WEEDSHOPCORE_API AWeedShopGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AWeedShopGameMode();

	// Forceer dat de speler-pawn ALTIJD spawnt (positie bijstellen indien nodig): bij co-op-join spawnt de
	// 2e speler op dezelfde PlayerStart als de host -> botsing -> met een strikte collision-methode geeft
	// de spawn null en crasht de engine met "Couldn't spawn player". AlwaysSpawn voorkomt dat.
	virtual APawn* SpawnDefaultPawnAtTransform_Implementation(AController* NewPlayerController, const FTransform& SpawnTransform) override;

	// Spawnt o.a. de centrale AActivitySpotManager (dev-tool: NPC's op vaste plek + tijdvak + anim).
	virtual void BeginPlay() override;
};
