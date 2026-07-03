// ULevelComponent — speler-/crew-level (1..100) op de GameState. XP komt binnen van de kern-loop
// (verkopen, oogsten, klanten werven). Server-authoritative; Level + XP repliceren naar de clients
// zodat de telefoon-UI ze kan tonen. Gedeeld in co-op (één crew-level).

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "LevelComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnLevelUp, int32, NewLevel);

UCLASS(ClassGroup = (WeedShop), meta = (BlueprintSpawnableComponent))
class WEEDSHOPCORE_API ULevelComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	ULevelComponent();

	static constexpr int32 MaxLevel = 100;
	static constexpr int32 ShopLicenseLevel = 50; // eind-mijlpaal: hier verdien je de shop-licentie

	UPROPERTY(BlueprintAssignable, Category = "WeedShop|Level")
	FOnLevelUp OnLevelUp;

	// Heb je de shop-licentie (op level 50) verdiend? Gate voor het weedshop-deel.
	UFUNCTION(BlueprintPure, Category = "WeedShop|Level")
	bool IsShopLicensed() const { return bShopLicensed; }
	void RestoreShopLicensed(bool b) { bShopLicensed = b; }

	// Server: ken XP toe (verkopen/oogsten/werven). Verwerkt level-ups (kan meerdere tegelijk).
	// StonedMult = de stoned-XP-bonus van de VERDIENENDE speler (1.0 = geen), per-verdiener meegegeven door
	// de caller (co-op: een nuchtere speler mag niet meeliften op de high-bonus van z'n maat).
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Level")
	void AddXP(int32 Amount, float StonedMult = 1.f);

	// Server: zet het level direct (bv. Testing-mode). Reset XP binnen het level; geen level-up-spam.
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Level")
	void GrantLevel(int32 NewLevel);

	// Server: herstel level + XP exact uit een save (geen level-up-events).
	void RestoreLevel(int32 InLevel, int32 InXP);

	UFUNCTION(BlueprintPure, Category = "WeedShop|Level")
	int32 GetLevel() const { return Level; }

	// XP verzameld binnen het huidige level.
	UFUNCTION(BlueprintPure, Category = "WeedShop|Level")
	int32 GetCurrentXP() const { return CurrentXP; }

	// XP nodig om van Level naar Level+1 te gaan (0 op max level).
	UFUNCTION(BlueprintPure, Category = "WeedShop|Level")
	int32 GetXPToNext() const { return XPForLevel(Level); }

	// 0..1 voortgang naar het volgende level (1 op max level).
	UFUNCTION(BlueprintPure, Category = "WeedShop|Level")
	float GetLevelFraction() const;

	// XP nodig om van Lvl naar Lvl+1 te gaan. Oplopende curve; 0 op/over MaxLevel.
	UFUNCTION(BlueprintPure, Category = "WeedShop|Level")
	static int32 XPForLevel(int32 Lvl);

protected:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(ReplicatedUsing = OnRep_Level)
	int32 Level = 1;

	UPROPERTY(Replicated)
	int32 CurrentXP = 0;

	UPROPERTY(Replicated)
	bool bShopLicensed = false;

	UFUNCTION()
	void OnRep_Level();
};
