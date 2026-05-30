// AWeedShopGameState — gedeelde, replicerende game-staat voor co-op. Host de gedeelde kas
// (UEconomyComponent). Later komt hier ook bv. de winkel open/dicht-status en de huidige dag.
//
// Editor-koppeling: zet deze (of een BP-subclass ervan) als **Game State Class** op je GameMode.
// Standaard FP-template: maak BP_WeedShopGameState (parent = AWeedShopGameState) en kies die in
// je GameMode-BP onder Classes -> Game State Class. Daarna is de kas overal te benaderen via
// GetGameState<AWeedShopGameState>()->GetEconomy().

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "WeedShopGameState.generated.h"

class UEconomyComponent;
class UDayCycleComponent;

UCLASS()
class WEEDSHOPCORE_API AWeedShopGameState : public AGameStateBase
{
	GENERATED_BODY()

public:
	AWeedShopGameState();

	// De gedeelde co-op-kas.
	UFUNCTION(BlueprintPure, Category = "WeedShop")
	UEconomyComponent* GetEconomy() const { return Economy; }

	// De gedeelde dag/nacht-klok.
	UFUNCTION(BlueprintPure, Category = "WeedShop")
	UDayCycleComponent* GetDayCycle() const { return DayCycle; }

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "WeedShop")
	TObjectPtr<UEconomyComponent> Economy;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "WeedShop")
	TObjectPtr<UDayCycleComponent> DayCycle;
};
