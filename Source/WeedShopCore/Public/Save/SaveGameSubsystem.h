// USaveGameSubsystem — slaat de gedeelde voortgang op/laadt 'm. Overleeft level-load
// (GameInstance-subsystem). Alleen de host/server schrijft en herstelt de gedeelde state;
// clients krijgen het via replicatie.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "SaveGameSubsystem.generated.h"

class AWeedShopGameState;

UCLASS()
class WEEDSHOPCORE_API USaveGameSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WeedShop|Save")
	FString SlotName = TEXT("WeedShopSave");

	// Schrijft de huidige gedeelde state naar de slot. False bij client of geen GameState.
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Save")
	bool SaveGame();

	// Laadt de slot en herstelt de gedeelde state (alleen server). False als er geen save is.
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Save")
	bool LoadGame();

	UFUNCTION(BlueprintPure, Category = "WeedShop|Save")
	bool HasSave() const;

protected:
	AWeedShopGameState* GetWeedGameState() const;
	bool HasAuthorityWorld() const;
};
