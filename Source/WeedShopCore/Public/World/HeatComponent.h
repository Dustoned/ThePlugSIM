// UHeatComponent — politie-"heat"/risico op de GameState (gedeeld, co-op). Stijgt bij riskant gedrag
// (straat-samples/dealen) en houdt een VLOER aan bij te veel potten (boven de apartment-cap). Zakt alleen
// OVERDAG (langzaam); 's nachts blijft het staan en zakt pas weer als het dag wordt. Alleen 's nachts en bij
// ECHT hoge heat (>=80) is er een kleine kans op een **bust** of **overval** (verlies cash). Rustig blijven +
// niet te veel potten = heat zakt overdag weg = geen events. Beveiliging-upgrade (HeatResist) dempt opbouw + kans.

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

	// Tunables. Heat zakt ALLEEN overdag (langzaam); 's nachts blijft het staan en zakt pas weer als het dag wordt.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WeedShop|Heat")
	float DayDecayPerSecond = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WeedShop|Heat")
	float NightDecayPerSecond = 0.0f;

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

	// "Te veel potten": boven dit aantal potten (rond je apartment) houdt elke EXTRA pot een heat-VLOER aan.
	// Je MAG overvol kweken, maar het trekt politie-aandacht. 1e apartment = 6; een groter apartment (later)
	// verhoogt deze cap. De grote Fabric-pot (meerdere planten) telt gewoon als 1 pot.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WeedShop|Heat")
	int32 PotCap = 6;

	// Heat-vloer per overtollige pot (gedempt door de beveiliging-upgrade), gecapt op MaxPotHeat.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WeedShop|Heat")
	float HeatPerExcessPot = 16.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WeedShop|Heat")
	float MaxPotHeat = 90.f;

	UPROPERTY(BlueprintAssignable, Category = "WeedShop|Heat")
	FOnHeatChanged OnHeatChanged;

	// Server: voeg heat toe (riskant gedrag). Dempt mee met de beveiliging-upgrade.
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Heat")
	void AddHeat(float Amount);

	UFUNCTION(BlueprintPure, Category = "WeedShop|Heat")
	float GetHeat() const { return Heat; }

	// Dev/test: forceer direct een overval of bust (negeert nacht + heat-cooldown). Server.
	void DevTriggerRobbery() { if (GetOwnerRole() == ROLE_Authority) { TriggerRobbery(); } }
	void DevTriggerBust()    { if (GetOwnerRole() == ROLE_Authority) { TriggerBust(); } }

	// Save/load: heat-waarde + event-cooldown-staat (bust/overval-timer + laatste-event-dag).
	void RestoreHeat(float V) { SetHeat(V); }
	float GetEventTimer() const { return EventTimer; }
	int32 GetLastEventDay() const { return LastEventDay; }
	void RestoreEventState(float Timer, int32 LastDay) { EventTimer = Timer; LastEventDay = LastDay; }

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

	// Pot-cap ("te veel potten"): periodieke telling -> heat-vloer + eenmalige waarschuwing bij over de cap.
	float PotScanTimer = 0.f;
	float CachedPotFloor = 0.f;
	bool bWasOverPotCap = false;
	float ComputePotHeatFloor(); // telt de potten rond het/de apartment(en) en geeft de heat-vloer terug
};
