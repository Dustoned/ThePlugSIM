// USaveGameSubsystem — slaat de gedeelde voortgang op/laadt 'm. Overleeft level-load
// (GameInstance-subsystem). Alleen de host/server schrijft en herstelt de gedeelde state;
// clients krijgen het via replicatie.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "SaveGameSubsystem.generated.h"

class AWeedShopGameState;
class UWeedShopSaveGame;
class APawn;

UCLASS()
class WEEDSHOPCORE_API USaveGameSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WeedShop|Save")
	FString SlotName = TEXT("WeedShopSave");

	// Schrijft de huidige gedeelde state + ALLE verbonden spelers (op username) naar de slot.
	// Alleen de host/server. False bij client of geen GameState.
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Save")
	bool SaveGame();

	// Laadt de slot, herstelt de gedeelde state + alle nu verbonden spelers (alleen server).
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Save")
	bool LoadGame();

	UFUNCTION(BlueprintPure, Category = "WeedShop|Save")
	bool HasSave() const;

	// Server: herstel één speler (op username) uit de geladen save, indien aanwezig en nog niet
	// hersteld deze sessie. Aangeroepen wanneer een (co-op) speler de wereld in komt.
	void RestorePlayerByPawn(APawn* Pawn);

protected:
	AWeedShopGameState* GetWeedGameState() const;
	bool HasAuthorityWorld() const;

	// Helpers om per-speler data te verzamelen/toe te passen + identiteit.
	static void PlayerKeys(const APawn* Pawn, FString& OutId, FString& OutName); // stabiele id + weergavenaam
	static bool Matches(const struct FPlayerSaveData& Rec, const FString& Id, const FString& Name);
	void GatherPlayer(APawn* Pawn, struct FPlayerSaveData& Out) const;
	void ApplyPlayer(APawn* Pawn, const struct FPlayerSaveData& Data);

	// In het geheugen gehouden geladen save (voor late-joiners die nog hersteld moeten worden).
	UPROPERTY()
	TObjectPtr<UWeedShopSaveGame> Loaded;

	// Welke usernames deze sessie al hersteld zijn (voorkomt dubbel herstel bij respawn).
	TSet<FString> RestoredPlayers;
};
