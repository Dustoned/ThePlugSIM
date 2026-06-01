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
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WeedShop|Save")
	FString SlotName = TEXT("WeedShopSave");

	// Aantal save-slots.
	static constexpr int32 NumSlots = 3;

	// Slot-keuze (welke slot Save/Load gebruiken).
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Save")
	void SetSlot(int32 Slot);
	UFUNCTION(BlueprintPure, Category = "WeedShop|Save")
	int32 GetSlot() const { return CurrentSlot; }

	// Bestaat er een save in dit slot? + korte samenvatting (false als leeg).
	UFUNCTION(BlueprintPure, Category = "WeedShop|Save")
	bool HasSaveInSlot(int32 Slot) const;
	bool GetSlotInfo(int32 Slot, FString& OutSummary) const;

	// Nieuw spel in dit slot starten (verse staat; oude save blijft tot je opnieuw opslaat).
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Save")
	void NewGameInSlot(int32 Slot);

	// Laad een specifiek slot.
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Save")
	bool LoadSlot(int32 Slot);

	// Continue: laad het laatst gebruikte slot, anders het eerste bestaande. False als er geen is.
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Save")
	bool QuickContinue();

	// Schrijft de huidige gedeelde state + ALLE verbonden spelers (op username) naar de slot.
	// Alleen de host/server. False bij client of geen GameState.
	// bAutosave = naar het aparte autosave-bestand schrijven (overschrijft je echte save NIET).
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Save")
	bool SaveGame(bool bAutosave = false);

	// Laadt de slot, herstelt de gedeelde state + alle nu verbonden spelers (alleen server).
	// bPreferNewest = neem het nieuwste van handmatig/autosave (voor Continue). Anders: altijd de
	// echte (handmatige) save, en alleen de autosave als er nog geen handmatige save bestaat.
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Save")
	bool LoadGame(bool bPreferNewest = false);

	UFUNCTION(BlueprintPure, Category = "WeedShop|Save")
	bool HasSave() const;

	// Server: herstel één speler (op username) uit de geladen save, indien aanwezig en nog niet
	// hersteld deze sessie. Aangeroepen wanneer een (co-op) speler de wereld in komt.
	void RestorePlayerByPawn(APawn* Pawn);

protected:
	AWeedShopGameState* GetWeedGameState() const;
	bool HasAuthorityWorld() const;
	FString SlotNameFor(int32 Slot) const;
	FString AutoSlotNameFor(int32 Slot) const; // apart autosave-bestand naast de echte save
	// Kies welk bestand geladen moet worden voor het huidige slot ("" = geen save aanwezig).
	FString ResolveLoadName(bool bPreferNewest) const;

	int32 CurrentSlot = 0;

	// Helpers om per-speler data te verzamelen/toe te passen + identiteit.
	static void PlayerKeys(const APawn* Pawn, FString& OutId, FString& OutName); // stabiele id + weergavenaam
	static bool Matches(const struct FPlayerSaveData& Rec, const FString& Id, const FString& Name);
	void GatherPlayer(APawn* Pawn, struct FPlayerSaveData& Out) const;
	void ApplyPlayer(APawn* Pawn, const struct FPlayerSaveData& Data);

	// Geplaatste wereld-objecten verzamelen / opnieuw spawnen.
	void GatherPlaced(class UWorld* World, TArray<struct FPlacedObjectSave>& Out) const;
	void RespawnPlaced(class UWorld* World, const TArray<struct FPlacedObjectSave>& In);

	// In het geheugen gehouden geladen save (voor late-joiners die nog hersteld moeten worden).
	UPROPERTY()
	TObjectPtr<UWeedShopSaveGame> Loaded;

	// Welke usernames deze sessie al hersteld zijn (voorkomt dubbel herstel bij respawn).
	TSet<FString> RestoredPlayers;
};
