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

	// 2-speler-cap op de sessie: co-op EN competitive zijn voor precies 2 spelers ontworpen/getest.
	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;

	// Zet de stabiele per-speler-id (PlugPid) op de AWeedShopPlayerState: URL-optie "PlugPid" (joiner) >
	// eigen login-id (host), met een "#2"-dedupe-suffix bij een botsing (2 instanties op 1 machine).
	virtual FString InitNewPlayer(APlayerController* NewPlayerController, const FUniqueNetIdRepl& UniqueId, const FString& Options, const FString& Portal = TEXT("")) override;

	// Forceer dat de speler-pawn ALTIJD spawnt (positie bijstellen indien nodig): bij co-op-join spawnt de
	// 2e speler op dezelfde PlayerStart als de host -> botsing -> met een strikte collision-methode geeft
	// de spawn null en crasht de engine met "Couldn't spawn player". AlwaysSpawn voorkomt dat.
	virtual APawn* SpawnDefaultPawnAtTransform_Implementation(AController* NewPlayerController, const FTransform& SpawnTransform) override;

	// Spawnt o.a. de centrale AActivitySpotManager (dev-tool: NPC's op vaste plek + tijdvak + anim).
	virtual void BeginPlay() override;

	// Houd een REMOTE joiner vast tot de host-wereld echt klaar is (kamer ingestreamd). Joinde een client
	// terwijl de host nog in het laadscherm zat, dan spawnde z'n pawn in een half-opgebouwde wereld
	// (collision/physics nog niet klaar) -> crash op de host. De host zelf (local controller) spawnt gewoon.
	virtual void HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer) override;

private:
	bool IsHostWorldReady() const;
	void FlushPendingJoiners();
	// Zet de zojuist-gespawnde speler DIRECT thuis via de DoorRetrofitter (server-authoritative), zodat de
	// gerepliceerde positie al "thuis" is voordat de joiner z'n pawn possesst -> geen zichtbare beach-tussenstaat
	// op de host. No-op als er geen retrofitter is (map zonder homing) of de thuis-plek nog niet bekend is (de
	// scan-pass-homing pakt 'm dan alsnog als vangnet).
	void HomePlayerViaRetrofitter(APlayerController* PC);

	TArray<TWeakObjectPtr<APlayerController>> PendingJoiners;
	FTimerHandle JoinFlushTimer;
};
