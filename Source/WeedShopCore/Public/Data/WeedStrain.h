// Strain-definitie voor DT_Strains. Tijd is gekoppeld aan yield + kwaliteit: beter spul groeit
// langer. Echte strain-namen (Northern Lights, OG Kush, Sour Diesel, ...).
//
// Editor-koppeling: importeer Data/DT_Strains.csv als DataTable (Row Type = WeedStrainRow).

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "WeedStrain.generated.h"

USTRUCT(BlueprintType)
struct FWeedStrainRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Strain")
	FText DisplayName;

	// Maximale THC% bij perfecte verzorging. Werkelijke % = Base * care * noise.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Strain")
	float BaseThcPercent = 12.f;

	// Maximale opbrengst in gram bij perfecte verzorging.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Strain")
	float BaseYieldGrams = 10.f;

	// Volledige groeicyclus (seed -> oogstklaar) in real-time minuten.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Strain")
	float GrowMinutes = 4.f;

	// Kosten van een zaadje (cents).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Strain")
	int32 SeedPriceCents = 500;

	// Inventory item-id dat de oogst oplevert (gedroogde bud van deze strain).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Strain")
	FName HarvestProductId = NAME_None;

	// Speler-level waarop dit zaadje in de winkel verschijnt. 0 = automatisch afleiden uit THC%.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Strain")
	int32 UnlockLevel = 0;
};
