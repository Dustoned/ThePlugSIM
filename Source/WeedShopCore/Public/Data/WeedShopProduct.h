// Productdefinitie voor DT_Products. Eén rij = één verkoopbaar product.
// Data-driven: balans en content komen uit CSV, zodat we kunnen tweaken zonder code.
//
// Editor-koppeling: importeer Data/DT_Products.csv als DataTable (Row Type = WeedShopProductRow)
// naar /Content/_Project/Data/DT_Products.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "WeedShopProduct.generated.h"

UENUM(BlueprintType)
enum class EProductCategory : uint8
{
	Weed		UMETA(DisplayName = "Wiet"),
	Edible		UMETA(DisplayName = "Edible"),
	Accessory	UMETA(DisplayName = "Accessoire")
};

USTRUCT(BlueprintType)
struct FWeedShopProductRow : public FTableRowBase
{
	GENERATED_BODY()

	// Naam zoals getoond in de UI.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Product")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Product")
	EProductCategory Category = EProductCategory::Weed;

	// Marktprijs in eurocents (referentieprijs; de prijs-slider werkt hier omheen).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Product")
	int32 MarketPriceCents = 0;

	// Vraag/populariteit 0..1 — hoe vaak klanten dit willen (spawn/order-gewicht).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Product", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Popularity = 0.5f;

	// Milestone die dit product ontgrendelt. None = vanaf het begin verkoopbaar.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Product")
	FName UnlockMilestone = NAME_None;

	// Placeholder-referenties (later invullen). Soft, zodat de DataTable niet alles inlaadt.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Product")
	TSoftObjectPtr<UStaticMesh> WorldMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Product")
	TSoftObjectPtr<UTexture2D> Icon;
};
