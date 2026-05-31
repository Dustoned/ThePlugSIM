// UStoreComponent — supplier op de GameState. Verkoopt zaden (uit DT_Strains) aan de speler:
// schrijft af van de kas en voegt een "Seed_<strain>" item toe aan de inventory van de koper.
// Server-authoritative voor de aankoop; catalogus-queries werken overal (lezen de DataTable).

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "StoreComponent.generated.h"

class UDataTable;
class UInventoryComponent;

UCLASS(ClassGroup = (WeedShop), meta = (BlueprintSpawnableComponent))
class WEEDSHOPCORE_API UStoreComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UStoreComponent();

	// Item-id van een zaadje voor een strain (bv. "Seed_NorthernLights").
	UFUNCTION(BlueprintPure, Category = "WeedShop|Store")
	static FName SeedItemId(FName StrainId);

	// Strain-naam terug uit een zaad-item-id (leeg als het geen zaad is).
	static FName StrainFromSeedItem(FName SeedId);

	// De zaden die te koop zijn (strain-rij-namen uit DT_Strains).
	UFUNCTION(BlueprintPure, Category = "WeedShop|Store")
	TArray<FName> GetSeedCatalog() const;

	// Weergave-info voor een zaad (voor de telefoon-UI).
	bool GetSeedDisplay(FName StrainId, FText& OutName, int32& OutPriceCents) const;

	// Server: koopt 1 zaadje van deze strain voor Buyer. False bij te weinig geld/onbekend.
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Store")
	bool BuySeed(FName StrainId, UInventoryComponent* Buyer);

	// --- Supplies (vloei e.d.), naast zaden ---
	UFUNCTION(BlueprintPure, Category = "WeedShop|Store")
	TArray<FName> GetSupplyCatalog() const;

	// Weergave-info voor een supply (naam + prijs + hoeveel je per koop krijgt).
	bool GetSupplyDisplay(FName SupplyId, FText& OutName, int32& OutPriceCents, int32& OutPackSize) const;

	// Server: koop een supply-pakket voor Buyer.
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Store")
	bool BuySupply(FName SupplyId, UInventoryComponent* Buyer);

	// --- Supplier-categorieën voor de telefoon (netjes gesorteerd) ---
	// 0=Seeds, 1=Papers, 2=Pots, 3=Soil, 4=Water.
	static constexpr int32 SupplierCatCount = 5;

	// Item-ids in een categorie (zaden voor cat 0, anders gefilterde supplies).
	UFUNCTION(BlueprintPure, Category = "WeedShop|Store")
	TArray<FName> GetSupplierCategory(int32 Cat) const;

	// Of deze categorie zaden bevat (dan kopen via BuySeed i.p.v. BuySupply).
	static bool IsSeedCategory(int32 Cat) { return Cat == 0; }

	// Verkoopprijs (cents) van een item; 0 als niet verkoopbaar (nu: pot-tiers).
	UFUNCTION(BlueprintPure, Category = "WeedShop|Store")
	int32 GetSellPriceCents(FName ItemId) const;

	// Server: verkoop 1 van dit item uit Seller (geeft geld terug). False als niet kan.
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Store")
	bool SellItem(FName ItemId, UInventoryComponent* Seller);

protected:
	UPROPERTY()
	TObjectPtr<UDataTable> StrainTable;
};
