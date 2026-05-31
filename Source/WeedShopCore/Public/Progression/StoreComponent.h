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

	// Server: koop 1 stuk van een willekeurig catalogus-item (zaad of supply); dispatcht zelf.
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Store")
	bool BuyAny(FName CatalogId, UInventoryComponent* Buyer);

	// Stukprijs (cents) van een catalogus-item (zaad of supply). 0 als onbekend. Voor de winkel-UI.
	UFUNCTION(BlueprintPure, Category = "WeedShop|Store")
	int32 GetCatalogPriceCents(FName CatalogId) const;

	// Weergave-naam van een catalogus-item (zaad of supply).
	UFUNCTION(BlueprintPure, Category = "WeedShop|Store")
	FText GetCatalogName(FName CatalogId) const;

	// --- Supplier-categorieën voor de telefoon (netjes gesorteerd) ---
	// 0=Seeds, 1=Papers, 2=Pots, 3=Soil, 4=Water, 5=Sell.
	static constexpr int32 SupplierCatCount = 6;
	static constexpr int32 SupplierCatSell = 5;

	// Item-ids in een categorie (zaden voor cat 0, anders gefilterde supplies).
	UFUNCTION(BlueprintPure, Category = "WeedShop|Store")
	TArray<FName> GetSupplierCategory(int32 Cat) const;

	// Of deze categorie zaden bevat (dan kopen via BuySeed i.p.v. BuySupply).
	static bool IsSeedCategory(int32 Cat) { return Cat == 0; }

	// Verkoopwaarde (cents) van een item bij de supplier: 70% van de koopprijs (seeds/supplies/
	// pots/soil/water) of een vaste waarde voor meubels. 0 = niet verkoopbaar (bv. wiet/joints
	// die je aan klanten verkoopt).
	UFUNCTION(BlueprintPure, Category = "WeedShop|Store")
	int32 GetSellValueCents(FName ItemId) const;

	// Server: verkoop 1 van dit item uit Seller (geeft geld terug). False als niet kan.
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Store")
	bool SellItem(FName ItemId, UInventoryComponent* Seller);

protected:
	UPROPERTY()
	TObjectPtr<UDataTable> StrainTable;
};
