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

// Co-op speelmodus (gekozen bij het hosten, gedeeld + gerepliceerd voor de hele sessie).
UENUM(BlueprintType)
enum class ECoopMode : uint8
{
	Coop         UMETA(DisplayName = "Co-op (samen)"),       // alles gedeeld: 1 crew, samen opbouwen
	Competitive  UMETA(DisplayName = "Competitive (versus)")  // ieder voor zich: eigen geld + relaties, klanten afpakken
};

// Eén regel op het competitive-scorebord (per speler: net worth = cash + bank).
USTRUCT(BlueprintType)
struct FCompetitorScore
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category = "WeedShop") FString Name;
	UPROPERTY(BlueprintReadOnly, Category = "WeedShop") int64 NetWorthCents = 0;
	UPROPERTY(BlueprintReadOnly, Category = "WeedShop") int64 CashCents = 0;
	UPROPERTY(BlueprintReadOnly, Category = "WeedShop") int64 BankCents = 0;
	UPROPERTY(BlueprintReadOnly, Category = "WeedShop") int64 EarnedCents = 0; // totaal verdiend (verkoop-omzet)
	UPROPERTY(BlueprintReadOnly, Category = "WeedShop") int32 Customers = 0;   // aantal vaste klanten
};

// Eén actieve bezorging: de wereldlocatie van het pakket (bij de voordeur). Gerepliceerd zodat map +
// kompas bij ALLE spelers een pakket-marker tonen tot het opgehaald is. ForPlayerId = stabiele id van de
// bestellende speler; in COMPETITIVE tonen map/kompas alleen de eigen marker (anders verklap je de kamer
// van de tegenstander). In co-op is ForPlayerId niet-filterend -> de gedeelde marker is juist gewenst.
USTRUCT()
struct FActiveDelivery
{
	GENERATED_BODY()
	UPROPERTY() int32 OrderId = 0;
	UPROPERTY() FVector World = FVector::ZeroVector;
	UPROPERTY() FString ForPlayerId; // StablePlayerId van de bestellende speler (leeg = gedeeld/co-op)
};

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

	// De GameState-eigen economy = de GEDEELDE crew-bank in co-op (bank-saldo van alle spelers samen).
	// In competitive niet gebruikt (daar heeft ieder z'n eigen bank op de pawn).
	UEconomyComponent* GetSharedEconomy() const { return Economy; }

	// De gedeelde dag/nacht-klok.
	UFUNCTION(BlueprintPure, Category = "WeedShop")
	UDayCycleComponent* GetDayCycle() const { return DayCycle; }

	// Gedeelde progressie (milestones + fase).
	UFUNCTION(BlueprintPure, Category = "WeedShop")
	UMilestoneComponent* GetMilestones() const { return Milestones; }

	// Gedeelde doelen/goals (joints, oogst, deals, geld) met rewards.
	UFUNCTION(BlueprintPure, Category = "WeedShop")
	class UGoalsComponent* GetGoals() const { return Goals; }

	// Gedeelde, gekochte upgrades.
	UFUNCTION(BlueprintPure, Category = "WeedShop")
	UUpgradeComponent* GetUpgrades() const { return Upgrades; }

	// Supplier (zaden kopen).
	UFUNCTION(BlueprintPure, Category = "WeedShop")
	UStoreComponent* GetStore() const { return Store; }

	// Telefoon-contacten + berichten.
	UFUNCTION(BlueprintPure, Category = "WeedShop")
	UContactsComponent* GetContacts() const { return Contacts; }

	// Gedeelde co-op wereld-state (deuren e.d. die per-client lokaal staan maar gesynct moeten zijn).
	class UWorldSyncComponent* GetWorldSync() const { return WorldSync; }

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

	// Vrij bouwen (testing/sandbox): plaats overal, ook buiten, zonder "alleen binnen"-regel en zonder
	// grondhoogte-beperking. De logische surface-regels blijven (lamp -> plafond, rek -> muur).
	UPROPERTY(Replicated)
	bool bFreeBuild = false;
	UFUNCTION(BlueprintPure, Category = "WeedShop")
	bool IsFreeBuild() const { return bFreeBuild; }
	void SetFreeBuild(bool b) { if (HasAuthority()) { bFreeBuild = b; } }

	// Dev-tools (F10-dev-menu, F7/F9-dev-keys, marker-tools): LOS van free-build (dat is een gameplay-
	// vlag voor het bouwen). Sessie-breed: elke speler mag 'm aanzetten via Ctrl+Shift+F10 (of `WeedDev`).
	UPROPERTY(Replicated)
	bool bDevTools = false;
	UFUNCTION(BlueprintPure, Category = "WeedShop")
	bool AreDevToolsEnabled() const { return bDevTools; }
	void SetDevTools(bool b) { if (HasAuthority()) { bDevTools = b; } }

	// Co-op speelmodus (gedeeld, gekozen bij hosten). Coop = alles samen; Competitive = ieder voor zich.
	UPROPERTY(Replicated)
	ECoopMode CoopMode = ECoopMode::Coop;
	UFUNCTION(BlueprintPure, Category = "WeedShop")
	ECoopMode GetCoopMode() const { return CoopMode; }
	UFUNCTION(BlueprintPure, Category = "WeedShop")
	bool IsCompetitive() const { return CoopMode == ECoopMode::Competitive; }
	void SetCoopMode(ECoopMode M) { if (HasAuthority()) { CoopMode = M; } }

	// Competitive scorebord (net worth per speler, gesorteerd hoog->laag). Leeg in co-op.
	UPROPERTY(Replicated)
	TArray<FCompetitorScore> Standings;
	UFUNCTION(BlueprintPure, Category = "WeedShop")
	const TArray<FCompetitorScore>& GetStandings() const { return Standings; }

	// Actieve bezorgingen (pakket-locaties bij de voordeur). Gerepliceerd -> map + kompas tonen overal
	// een pakket-marker tot opgehaald. Server-only muteren via Add/Remove (op order + bij pickup).
	UPROPERTY(Replicated)
	TArray<FActiveDelivery> ActiveDeliveries;
	const TArray<FActiveDelivery>& GetActiveDeliveries() const { return ActiveDeliveries; }
	// ForPlayerId = stabiele id van de bestellende speler (leeg = gedeeld/co-op). In competitive filteren
	// map + kompas hierop; in co-op wordt 'ie niet gebruikt (gedeelde marker). Achterwaarts compatibel:
	// zonder ForPlayerId -> gedeeld (oud gedrag).
	void AddDeliveryTarget(int32 OrderId, const FVector& World, const FString& ForPlayerId = FString());
	void RemoveDeliveryTarget(int32 OrderId);

	// Server-unieke bezorg-id over spelers heen: de gedeelde ActiveDeliveries wordt op OrderId gekeyed, dus
	// host + joiner mogen NOOIT hetzelfde id uitdelen (per-component NextOrderId begon bij beide op 1 -> botsing).
	// Alleen de server deelt id's uit; clients krijgen 'm mee via de RPC-flow. 0 = ongeldig (geen authority).
	int32 AllocDeliveryId() { return HasAuthority() ? NextDeliveryId++ : 0; }

	// Gedeelde starter-huur-status: de huur van het start-huis is GEDEELDE wereld-staat (1 huis). De starter-deur
	// is bReplicates=false (deterministisch-lokaal per proces), dus we repliceren de OVERDUE-lock hier zodat host
	// EN joiner hun lokale deur consistent vergrendelen + de "pay at the door"-prompt tonen. Alleen de server zet
	// 'm (DoorRetrofitter int de huur / ServerPayRent wist 'm bij betaling).
	UPROPERTY(Replicated)
	bool bStarterRentOverdue = false;
	UPROPERTY(Replicated)
	int64 StarterRentCents = 0;
	bool IsStarterRentOverdue() const { return bStarterRentOverdue; }
	int64 GetStarterRentCents() const { return StarterRentCents; }
	void SetStarterRentOverdue(bool bOverdue, int64 Cents) { if (HasAuthority()) { bStarterRentOverdue = bOverdue; StarterRentCents = Cents; } }

	// COMPETITIVE: huur-achterstand PER DEUR (WorldSync-deur-id). In competitive woont ieder in z'n EIGEN
	// comp-kamer (603/602), niet in het gedeelde starter-penthouse - 1 gedeelde vlag zou daar op de verkeerde
	// (lege) deur landen. Gerepliceerd zodat host EN joiner hun LOKALE deur-kopie locken (deuren zijn
	// bReplicates=false). Alleen de server muteert; co-op/solo blijft het enkele-vlag-pad hierboven gebruiken.
	UPROPERTY(Replicated)
	TArray<uint32> RentOverdueDoorIds;
	const TArray<uint32>& GetRentOverdueDoorIds() const { return RentOverdueDoorIds; }
	void AddRentOverdueDoor(uint32 Id) { if (HasAuthority() && Id != 0) { RentOverdueDoorIds.AddUnique(Id); } }
	void ClearRentOverdueDoor(uint32 Id) { if (HasAuthority()) { RentOverdueDoorIds.Remove(Id); } }
	bool IsRentOverdueDoor(uint32 Id) const { return Id != 0 && RentOverdueDoorIds.Contains(Id); }

protected:
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION()
	void AutoSave();

	// Server: herbereken het competitive-scorebord (net worth per speler) en repliceer het.
	UFUNCTION()
	void UpdateStandings();

	FTimerHandle AutoSaveTimer;
	FTimerHandle StandingsTimer;

	// Server-unieke, oplopende bezorg-id (allocator: AllocDeliveryId). Niet gerepliceerd: alleen de server
	// deelt id's uit en stuurt ze via de bestaande RPC-flow door naar de client-component.
	int32 NextDeliveryId = 1;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "WeedShop")
	TObjectPtr<UEconomyComponent> Economy;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "WeedShop")
	TObjectPtr<UDayCycleComponent> DayCycle;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "WeedShop")
	TObjectPtr<class UGoalsComponent> Goals;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "WeedShop")
	TObjectPtr<UMilestoneComponent> Milestones;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "WeedShop")
	TObjectPtr<UUpgradeComponent> Upgrades;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "WeedShop")
	TObjectPtr<UStoreComponent> Store;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "WeedShop")
	TObjectPtr<UContactsComponent> Contacts;

	UPROPERTY()
	TObjectPtr<class UWorldSyncComponent> WorldSync;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "WeedShop")
	TObjectPtr<UNpcRegistryComponent> NpcRegistry;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "WeedShop")
	TObjectPtr<UHeatComponent> Heat;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "WeedShop")
	TObjectPtr<ULevelComponent> Leveling;
};
