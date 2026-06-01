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
class UMilestoneComponent;
class UUpgradeComponent;
class UStoreComponent;
class UContactsComponent;
class UNpcRegistryComponent;
class UHeatComponent;
class ULevelComponent;

UCLASS()
class WEEDSHOPCORE_API AWeedShopGameState : public AGameStateBase
{
	GENERATED_BODY()

public:
	AWeedShopGameState();

	// Portemonnee: ieder z'n eigen (op de pawn). Dit geeft de LOKALE speler z'n portemonnee terug
	// (voor HUD/UI/heat/save op de host). Valt terug op de GameState-kas vóór er een pawn is.
	UFUNCTION(BlueprintPure, Category = "WeedShop")
	UEconomyComponent* GetEconomy() const;

	// De gedeelde dag/nacht-klok.
	UFUNCTION(BlueprintPure, Category = "WeedShop")
	UDayCycleComponent* GetDayCycle() const { return DayCycle; }

	// Gedeelde progressie (milestones + fase).
	UFUNCTION(BlueprintPure, Category = "WeedShop")
	UMilestoneComponent* GetMilestones() const { return Milestones; }

	// Gedeelde, gekochte upgrades.
	UFUNCTION(BlueprintPure, Category = "WeedShop")
	UUpgradeComponent* GetUpgrades() const { return Upgrades; }

	// Supplier (zaden kopen).
	UFUNCTION(BlueprintPure, Category = "WeedShop")
	UStoreComponent* GetStore() const { return Store; }

	// Telefoon-contacten + berichten.
	UFUNCTION(BlueprintPure, Category = "WeedShop")
	UContactsComponent* GetContacts() const { return Contacts; }

	// Register van alle NPC's met persistente per-persoon stats.
	UFUNCTION(BlueprintPure, Category = "WeedShop")
	UNpcRegistryComponent* GetNpcRegistry() const { return NpcRegistry; }

	UFUNCTION(BlueprintPure, Category = "WeedShop")
	UHeatComponent* GetHeat() const { return Heat; }

	// Gedeeld crew-level (1..100) + XP.
	UFUNCTION(BlueprintPure, Category = "WeedShop")
	ULevelComponent* GetLeveling() const { return Leveling; }

	// Host-autosave: elke X seconden de volledige staat wegschrijven (0 = uit).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WeedShop|Save")
	float AutoSaveSeconds = 180.f;

	// Telt op bij elke succesvolle save (repliceert) -> save-indicator bij alle spelers.
	UPROPERTY(Replicated)
	int32 SaveCounter = 0;

	UFUNCTION(BlueprintPure, Category = "WeedShop|Save")
	int32 GetSaveCounter() const { return SaveCounter; }

	// Server: meld dat er net opgeslagen is (laat de save-indicator bij iedereen knipperen).
	void NotifySaved() { if (HasAuthority()) { ++SaveCounter; } }

	// Idem voor laden -> toont "Loaded" bij iedereen.
	UPROPERTY(Replicated)
	int32 LoadCounter = 0;
	UFUNCTION(BlueprintPure, Category = "WeedShop|Save")
	int32 GetLoadCounter() const { return LoadCounter; }
	void NotifyLoaded() { if (HasAuthority()) { ++LoadCounter; } }

protected:
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION()
	void AutoSave();

	FTimerHandle AutoSaveTimer;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "WeedShop")
	TObjectPtr<UEconomyComponent> Economy;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "WeedShop")
	TObjectPtr<UDayCycleComponent> DayCycle;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "WeedShop")
	TObjectPtr<UMilestoneComponent> Milestones;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "WeedShop")
	TObjectPtr<UUpgradeComponent> Upgrades;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "WeedShop")
	TObjectPtr<UStoreComponent> Store;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "WeedShop")
	TObjectPtr<UContactsComponent> Contacts;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "WeedShop")
	TObjectPtr<UNpcRegistryComponent> NpcRegistry;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "WeedShop")
	TObjectPtr<UHeatComponent> Heat;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "WeedShop")
	TObjectPtr<ULevelComponent> Leveling;
};
