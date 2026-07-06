// UUpgradeComponent — gedeelde upgrades op de GameState. Koop met de kas; effecten worden
// opgeteld per tag zodat andere systemen ze kunnen uitlezen (bv. plant: "GrowthSpeed").
// Server-authoritative; gekochte upgrades repliceren.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "UpgradeComponent.generated.h"

class UDataTable;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnUpgradePurchased, FName, UpgradeId);

UCLASS(ClassGroup = (WeedShop), meta = (BlueprintSpawnableComponent))
class WEEDSHOPCORE_API UUpgradeComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UUpgradeComponent();

	UPROPERTY(BlueprintAssignable, Category = "WeedShop|Upgrades")
	FOnUpgradePurchased OnUpgradePurchased;

	// Server: koop een upgrade. Checkt fase + saldo, schrijft af van de koper z'n bank (PayFrom;
	// valt terug op de lokale portemonnee), markeert gekocht. Geeft succes.
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Upgrades")
	bool BuyUpgrade(FName UpgradeId, class UEconomyComponent* PayFrom = nullptr);

	UFUNCTION(BlueprintPure, Category = "WeedShop|Upgrades")
	bool IsPurchased(FName UpgradeId) const { return Purchased.Contains(UpgradeId); }

	// Vast upgrade-id van het polshorloge (ND7.16): code-gedefinieerd (geen DataTable-rij nodig).
	static const FName WatchUpgradeId;

	// Horloge gekocht? De HUD-klok linksboven toont alleen met horloge (StatusHudWidget).
	// CO-OP = gedeeld (1 koop voor de crew); COMPETITIVE = per speler (opslag via "Upg_Watch#<spelerId>"-
	// suffix in dezelfde Purchased-lijst, zelfde patroon als de NPC-registry -> save/replicatie gratis).
	UFUNCTION(BlueprintPure, Category = "WeedShop|Upgrades")
	bool HasWatch(const APawn* ForPawn = nullptr) const;

	// Opslag-sleutel voor het horloge: competitive + pawn -> per-speler-suffix, anders het gedeelde id.
	FName WatchKeyFor(const APawn* ForPawn) const;

	// Som van EffectMagnitude over alle gekochte upgrades met deze tag (0 als geen).
	UFUNCTION(BlueprintPure, Category = "WeedShop|Upgrades")
	float GetEffectTotal(FName EffectTag) const;

	UFUNCTION(BlueprintPure, Category = "WeedShop|Upgrades")
	int32 GetPurchasedCount() const { return Purchased.Num(); }

	// Lijst van gekochte upgrade-ids (voor save).
	const TArray<FName>& GetPurchasedIds() const { return Purchased; }

	// Server-only: herstel de gekochte upgrades (voor load).
	void RestorePurchased(const TArray<FName>& InIds);

	// Alle upgrade-id's (CSV-volgorde) — de telefoon-catalogus.
	UFUNCTION(BlueprintPure, Category = "WeedShop|Upgrades")
	TArray<FName> GetAllUpgradeIds() const;

	// Weergave-info voor één upgrade (voor de telefoon-UI). False als onbekend.
	bool GetUpgradeDisplay(FName UpgradeId, FText& OutName, int32& OutCostCents, bool& bOutPurchased, bool& bOutAvailable, const APawn* ForPawn = nullptr) const;

protected:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY()
	TObjectPtr<UDataTable> UpgradeTable;

	UPROPERTY(Replicated)
	TArray<FName> Purchased;
};
