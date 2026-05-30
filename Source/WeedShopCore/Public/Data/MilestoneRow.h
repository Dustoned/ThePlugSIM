// Milestone-definitie voor DT_Milestones. Een drempel aan totaal-verdiend ontgrendelt iets
// (product/fase) en stuurt de fase-overgang dealer -> winkel -> franchise.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "MilestoneRow.generated.h"

// De drie hoofdfases van de game.
UENUM(BlueprintType)
enum class EShopPhase : uint8
{
	StreetDealer	UMETA(DisplayName = "Straatdealer"),
	Shop			UMETA(DisplayName = "Winkel"),
	Franchise		UMETA(DisplayName = "Franchise")
};

USTRUCT(BlueprintType)
struct FMilestoneRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Milestone")
	FText Description;

	// Totaal-verdiend (cents) dat deze milestone ontgrendelt.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Milestone")
	int64 ThresholdEarnedCents = 0;

	// Product dat ontgrendeld wordt (rij-naam in DT_Products). None = geen.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Milestone")
	FName UnlockProductId = NAME_None;

	// Fase die deze milestone activeert (alleen omhoog).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Milestone")
	EShopPhase UnlockPhase = EShopPhase::StreetDealer;
};
