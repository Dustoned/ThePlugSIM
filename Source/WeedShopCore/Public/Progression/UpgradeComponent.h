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

	// Server: koop een upgrade. Checkt fase + saldo, schrijft af, markeert gekocht. Geeft succes.
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Upgrades")
	bool BuyUpgrade(FName UpgradeId);

	UFUNCTION(BlueprintPure, Category = "WeedShop|Upgrades")
	bool IsPurchased(FName UpgradeId) const { return Purchased.Contains(UpgradeId); }

	// Som van EffectMagnitude over alle gekochte upgrades met deze tag (0 als geen).
	UFUNCTION(BlueprintPure, Category = "WeedShop|Upgrades")
	float GetEffectTotal(FName EffectTag) const;

	UFUNCTION(BlueprintPure, Category = "WeedShop|Upgrades")
	int32 GetPurchasedCount() const { return Purchased.Num(); }

protected:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY()
	TObjectPtr<UDataTable> UpgradeTable;

	UPROPERTY(Replicated)
	TArray<FName> Purchased;
};
