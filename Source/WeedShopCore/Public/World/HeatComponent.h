// UHeatComponent — politie-"heat"/risico op de GameState (gedeeld, co-op). Stijgt ALLEEN bij riskant
// gedrag (straat-samples/dealen) en zakt ALTIJD vanzelf (langzamer 's nachts). Alleen 's nachts en bij
// ECHT hoge heat is er een kleine kans op een **bust** of **overval** (verlies cash). Rustig blijven =
// heat zakt naar 0 = geen events. Beveiliging-upgrade (HeatResist) dempt opbouw en kans.

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

	// Tunables. Heat zakt ALTIJD (geen passieve opbouw meer); 's nachts iets langzamer.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WeedShop|Heat")
	float DayDecayPerSecond = 0.8f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WeedShop|Heat")
	float NightDecayPerSecond = 0.3f;

	// Vanaf dit heat-niveau kan er 's nachts (zelden) een bust/overval gebeuren.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WeedShop|Heat")
	float BustThreshold = 80.f;

	// Hoe vaak (sec) en met welke basiskans een risico-event geprobeerd wordt.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WeedShop|Heat")
	float EventIntervalSeconds = 12.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WeedShop|Heat")
	float EventChance = 0.12f;

	// Na een bust/overval: zoveel in-game dagen GEEN nieuw event (rust).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WeedShop|Heat")
	int32 EventCooldownDays = 3;

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
	int32 LastEventDay = -1000; // dag van de laatste bust/overval (voor de dagen-cooldown)
};
