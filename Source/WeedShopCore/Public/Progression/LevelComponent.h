// ULevelComponent — speler-/crew-level (1..100) op de GameState. XP komt binnen van de kern-loop
// (verkopen, oogsten, klanten werven). Server-authoritative; Level + XP repliceren naar de clients
// zodat de telefoon-UI ze kan tonen.
//
// CO-OP (samen) = een GEDEELD crew-level/XP/licentie (opgeslagen in Shared). COMPETITIVE (versus) =
// per-speler level/XP/licentie (Players-array, gekeyed op StablePlayerId), zodat elke speler los klimt.
// Zelfde registry-keyed patroon als UNpcRegistryComponent. Pawn==nullptr OF !competitive -> altijd Shared.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "LevelComponent.generated.h"

class APawn;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnLevelUp, int32, NewLevel);

// De level-scalars (gedeeld in co-op; per-speler in competitive).
USTRUCT()
struct FLevelState
{
	GENERATED_BODY()

	UPROPERTY() int32 Level = 1;
	UPROPERTY() int32 CurrentXP = 0;
	UPROPERTY() bool bShopLicensed = false;
};

// Een per-speler level-entry (competitive), gekeyed op StablePlayerId.
USTRUCT()
struct FLevelPlayerEntry
{
	GENERATED_BODY()

	UPROPERTY() FName Key = NAME_None;
	UPROPERTY() FLevelState State;
};

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

	// XP nodig om van Lvl naar Lvl+1 te gaan. Oplopende curve; 0 op/over MaxLevel.
	UFUNCTION(BlueprintPure, Category = "WeedShop|Level")
	static int32 XPForLevel(int32 Lvl);

	// ================= Per-speler API (competitive) =================
	// Server: ken XP toe aan de VERDIENENDE speler. Verwerkt level-ups (kan meerdere tegelijk).
	// Co-op (nullptr/niet-competitive) -> gedeeld crew-level. StonedMult = stoned-XP-bonus van de verdiener.
	void  AddXPFor(const APawn* Earner, int32 Amount, float StonedMult = 1.f);

	// Read/gate; nullptr => Shared (co-op/fallback).
	int32 GetLevelFor(const APawn* Pawn) const;
	int32 GetCurrentXPFor(const APawn* Pawn) const;
	int32 GetXPToNextFor(const APawn* Pawn) const;
	float GetLevelFractionFor(const APawn* Pawn) const;
	bool  IsShopLicensedFor(const APawn* Pawn) const;

	// Dev/save: zet het level direct / herstel exact uit een save.
	void  GrantLevelFor(const APawn* Pawn, int32 NewLevel);
	void  RestoreLevelFor(const FName& Key, int32 InLevel, int32 InXP, bool bLicensed); // Key==NAME_None => Shared

	// ================= Compat-wrappers (Shared-key) =================
	// Behouden zodat niet-geconverteerde call-sites + Blueprint-refs blijven werken -> allemaal naar Shared.

	// Heb je de shop-licentie (op level 50) verdiend? Gate voor het weedshop-deel.
	UFUNCTION(BlueprintPure, Category = "WeedShop|Level")
	bool IsShopLicensed() const { return IsShopLicensedFor(nullptr); }
	void RestoreShopLicensed(bool b) { Shared.bShopLicensed = b; }

	// Server: ken XP toe (verkopen/oogsten/werven). Verwerkt level-ups (kan meerdere tegelijk).
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Level")
	void AddXP(int32 Amount, float StonedMult = 1.f) { AddXPFor(nullptr, Amount, StonedMult); }

	// Server: zet het level direct (bv. Testing-mode). Reset XP binnen het level; geen level-up-spam.
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Level")
	void GrantLevel(int32 NewLevel) { GrantLevelFor(nullptr, NewLevel); }

	// Server: herstel level + XP exact uit een save (geen level-up-events).
	void RestoreLevel(int32 InLevel, int32 InXP) { RestoreLevelFor(NAME_None, InLevel, InXP, Shared.bShopLicensed); }

	UFUNCTION(BlueprintPure, Category = "WeedShop|Level")
	int32 GetLevel() const { return GetLevelFor(nullptr); }

	// XP verzameld binnen het huidige level.
	UFUNCTION(BlueprintPure, Category = "WeedShop|Level")
	int32 GetCurrentXP() const { return GetCurrentXPFor(nullptr); }

	// XP nodig om van Level naar Level+1 te gaan (0 op max level).
	UFUNCTION(BlueprintPure, Category = "WeedShop|Level")
	int32 GetXPToNext() const { return GetXPToNextFor(nullptr); }

	// 0..1 voortgang naar het volgende level (1 op max level).
	UFUNCTION(BlueprintPure, Category = "WeedShop|Level")
	float GetLevelFraction() const { return GetLevelFractionFor(nullptr); }

	// ================= Save-hook =================
	// Directe lees/schrijf van de Shared- + Players-tabellen voor de save-migratie (competitive per-speler).
	const FLevelState& GetSharedState() const { return Shared; }
	const TArray<FLevelPlayerEntry>& GetPlayerEntries() const { return Players; }

protected:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// Resolvers: kies de juiste state voor een pawn (co-op vs competitive).
	FLevelState&       StateForPawn(const APawn* Pawn);            // schrijf (server): lazy-create in competitive
	const FLevelState& StateForPawnConst(const APawn* Pawn) const; // lees (const, geen create): Shared als geen entry

	// Kern van AddXP: verwerkt level-ups op een enkele state. bCompetitive bepaalt of de melding per-pawn gaat.
	void ProcessXP(FLevelState& St, const APawn* Earner, int32 Amount, float StonedMult, bool bCompetitive);

	// Shared = co-op/fallback (gedeeld crew-level). Players = per-speler (competitive).
	UPROPERTY(Replicated)
	FLevelState Shared;

	UPROPERTY(Replicated)
	TArray<FLevelPlayerEntry> Players;

	UFUNCTION()
	void OnRep_Level();
};
