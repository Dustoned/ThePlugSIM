// UHeatComponent — politie-"heat"/risico op de GameState. Stijgt bij riskant gedrag
// (straat-samples/dealen) en houdt een VLOER aan bij te veel potten (boven de apartment-cap). Zakt alleen
// OVERDAG (langzaam); 's nachts blijft het staan en zakt pas weer als het dag wordt. Alleen 's nachts en bij
// ECHT hoge heat (>=80) is er een kleine kans op een **bust** of **overval** (verlies cash). Rustig blijven +
// niet te veel potten = heat zakt overdag weg = geen events. Beveiliging-upgrade (HeatResist) dempt opbouw + kans.
//
// CO-OP (samen) = GEDEELDE heat (Shared). COMPETITIVE (versus) = per-speler heat (Players-array, gekeyed op
// StablePlayerId): elke speler heeft z'n eigen risico op basis van de potten rond DIENS eigen home + eigen gedrag.
// Zelfde registry-keyed patroon als UNpcRegistryComponent. Pawn==nullptr OF !competitive -> altijd Shared.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "HeatComponent.generated.h"

class APawn;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnHeatChanged, float, NewHeat);

// De heat-scalars (gedeeld in co-op; per-speler in competitive).
USTRUCT()
struct FHeatState
{
	GENERATED_BODY()

	UPROPERTY() float Heat = 0.f;
	UPROPERTY() float EventTimer = 0.f;
	UPROPERTY() int32 LastEventDay = -1000; // dag van de laatste bust/overval (voor de dagen-cooldown)
};

// Een per-speler heat-entry (competitive), gekeyed op StablePlayerId.
USTRUCT()
struct FHeatPlayerEntry
{
	GENERATED_BODY()

	UPROPERTY() FName Key = NAME_None;
	UPROPERTY() FHeatState State;
};

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

	// ================= Per-speler API (competitive) =================
	// Server: voeg heat toe aan de INSTIGERENDE speler (riskant gedrag). Dempt mee met de beveiliging-upgrade.
	// Co-op (nullptr/niet-competitive) -> gedeelde heat.
	void  AddHeatFor(const APawn* Instigator, float Amount);
	// Lees; nullptr => Shared (co-op/fallback).
	float GetHeatFor(const APawn* Pawn) const;
	// Save/load: herstel de heat-staat op een sleutel (NAME_None => Shared).
	void  RestoreHeatFor(const FName& Key, float V, float Timer, int32 LastDay);

	// ================= Compat-wrappers (Shared-key) =================
	// Behouden zodat niet-geconverteerde call-sites + Blueprint-refs blijven werken -> allemaal naar Shared.

	// Server: voeg heat toe (riskant gedrag). Dempt mee met de beveiliging-upgrade.
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Heat")
	void AddHeat(float Amount) { AddHeatFor(nullptr, Amount); }

	UFUNCTION(BlueprintPure, Category = "WeedShop|Heat")
	float GetHeat() const { return GetHeatFor(nullptr); }

	// Dev/test: forceer direct een overval of bust (negeert nacht + heat-cooldown). Server.
	// Beboet alle spelers (spiegelt de bestaande co-op bust/overval die per-pawn cash pakt).
	void DevTriggerRobbery() { if (GetOwnerRole() == ROLE_Authority) { TriggerRobbery(); } }
	void DevTriggerBust()    { if (GetOwnerRole() == ROLE_Authority) { TriggerBust(); } }

	// Save/load: heat-waarde + event-cooldown-staat (bust/overval-timer + laatste-event-dag). -> Shared.
	void RestoreHeat(float V) { RestoreHeatFor(NAME_None, V, Shared.EventTimer, Shared.LastEventDay); }
	float GetEventTimer() const { return Shared.EventTimer; }
	int32 GetLastEventDay() const { return Shared.LastEventDay; }
	void RestoreEventState(float Timer, int32 LastDay) { Shared.EventTimer = Timer; Shared.LastEventDay = LastDay; }

	// ================= Save-hook =================
	// Directe lees van de Shared- + Players-tabellen voor de save-migratie (competitive per-speler).
	const FHeatState& GetSharedState() const { return Shared; }
	const TArray<FHeatPlayerEntry>& GetPlayerEntries() const { return Players; }

protected:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// Shared = co-op/fallback (gedeelde heat). Players = per-speler (competitive).
	UPROPERTY(Replicated)
	FHeatState Shared;

	UPROPERTY(Replicated)
	TArray<FHeatPlayerEntry> Players;

	// Bewust no-op-achtig restant (props zijn plain Replicated, geen ReplicatedUsing): de UI polt de getters.
	UFUNCTION()
	void OnRep_Heat();

	// Resolvers: kies de juiste state voor een pawn (co-op vs competitive).
	FHeatState&       StateForPawn(const APawn* Pawn);            // schrijf (server): lazy-create in competitive
	const FHeatState& StateForPawnConst(const APawn* Pawn) const; // lees (const, geen create): Shared als geen entry

	// Zet de heat van een concrete state (clamp + OnHeatChanged voor de eigen/gedeelde balk).
	void SetHeatState(FHeatState& St, float NewHeat);

	// Beveiliging-dempingsfactor 0..0.9 uit de upgrades.
	float GetSecurityResist() const;

	// Server: één tick-stap (decay + event-evaluatie) op een enkele state. EventTargetPawn = de speler die
	// bij een bust/overval beboet wordt (nullptr = iedereen, co-op). bAllowEvents=false -> alleen decayen
	// (competitive-entry zonder levende pawn: geen events, geen notificaties).
	void TickState(FHeatState& St, float DeltaTime, APawn* EventTargetPawn, bool bNight, int32 CurDay, float Resist, float PotFloor, bool bAllowEvents);

	// Server: lichte bust (politie pakt een deel van de kas). OnlyPawn == nullptr (co-op/solo/dev) ->
	// beboet ALLE spelers per-pawn (bestaand gedrag); OnlyPawn gezet (competitive) -> alleen DIE speler.
	void TriggerBust(APawn* OnlyPawn = nullptr);

	// Server: overval (verlies cash + apartment leeggehaald). OnlyPawn == nullptr -> iedereen + alle homes
	// (bestaand gedrag); OnlyPawn gezet (competitive) -> alleen DIENS cash + DIENS eigen home.
	void TriggerRobbery(APawn* OnlyPawn = nullptr);

	// Pot-cap ("te veel potten"): periodieke telling -> heat-vloer + eenmalige waarschuwing bij over de cap.
	// HomeFilterPawn != nullptr (competitive) -> tel alleen de potten rond DIENS home; nullptr -> alle homes (co-op).
	float PotScanTimer = 0.f;
	float CachedPotFloor = 0.f;         // co-op: gedeelde gecachte vloer
	bool bWasOverPotCap = false;        // co-op: gedeelde once-latch voor de over-cap-toast

	// Competitive (transient, NIET gerepliceerd): pot-vloer-cache + over-cap-once-latch per speler-key,
	// zodat de comp-tak de 3s-rescan-gate respecteert en de toast 1x vuurt (reset bij eronder zakken).
	TMap<FName, float> CachedPotFloorByKey;
	TSet<FName> OverPotCapKeys;

	float ComputePotHeatFloor(const APawn* HomeFilterPawn); // telt de potten rond het/de apartment(en) -> heat-vloer
};
