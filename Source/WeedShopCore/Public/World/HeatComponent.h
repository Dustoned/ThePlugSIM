// UHeatComponent — politie-"heat"/risico op de GameState (gedeeld, co-op). Stijgt 's nachts en
// bij riskant gedrag (straat-samples/dealen), zakt overdag. Hoge heat 's nachts kan een lichte
// **bust** (verlies cash) of **overval** (verlies cash) triggeren. Beveiliging-upgrade (HeatResist)
// dempt opbouw en kans. Server-authoritative; heat repliceert naar de clients.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "HeatComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnHeatChanged, float, NewHeat);

UCLASS(ClassGroup = (WeedShop), meta = (BlueprintSpawnableComponent))
class WEEDSHOPCORE_API UHeatComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UHeatComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// Tunables.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WeedShop|Heat")
	float NightHeatPerSecond = 0.4f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WeedShop|Heat")
	float DayDecayPerSecond = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WeedShop|Heat")
	float BustThreshold = 75.f;

	UPROPERTY(BlueprintAssignable, Category = "WeedShop|Heat")
	FOnHeatChanged OnHeatChanged;

	// Server: voeg heat toe (riskant gedrag). Dempt mee met de beveiliging-upgrade.
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Heat")
	void AddHeat(float Amount);

	UFUNCTION(BlueprintPure, Category = "WeedShop|Heat")
	float GetHeat() const { return Heat; }

protected:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(ReplicatedUsing = OnRep_Heat)
	float Heat = 0.f;

	UFUNCTION()
	void OnRep_Heat();

	void SetHeat(float NewHeat);

	// Beveiliging-dempingsfactor 0..0.9 uit de upgrades.
	float GetSecurityResist() const;

	// Server: lichte bust (politie pakt een deel van de kas).
	void TriggerBust();

	// Server: overval (verlies cash).
	void TriggerRobbery();

	float EventTimer = 0.f;
};
