// Upgrade-definitie voor DT_Upgrades. Koop met je kas; een upgrade heeft een effect-tag +
// grootte die andere systemen optellen (bv. plant leest "GrowthSpeed" en "CareRetention").

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Data/MilestoneRow.h" // EShopPhase
#include "UpgradeRow.generated.h"

UENUM(BlueprintType)
enum class EUpgradeCategory : uint8
{
	GrowGear	UMETA(DisplayName = "Kweek-gear"),
	Storage		UMETA(DisplayName = "Opslag"),
	Security	UMETA(DisplayName = "Beveiliging"),
	Premises	UMETA(DisplayName = "Pand/Personeel")
};

USTRUCT(BlueprintType)
struct FUpgradeRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Upgrade")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Upgrade")
	EUpgradeCategory Category = EUpgradeCategory::GrowGear;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Upgrade")
	int32 CostCents = 1000;

	// Minimale fase waarin de upgrade koopbaar is.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Upgrade")
	EShopPhase RequiredPhase = EShopPhase::StreetDealer;

	// Effect-identificatie (bv. "GrowthSpeed", "CareRetention", "HeatResist") + grootte.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Upgrade")
	FName EffectTag = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Upgrade")
	float EffectMagnitude = 0.f;
};
