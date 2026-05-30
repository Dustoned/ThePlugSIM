// Klanttype-definitie voor DT_CustomerTypes. Bepaalt voorkeur, geduld, budget, spawn-gewicht
// (apart voor dag/nacht) en start-attributen + groeisnelheid. De runtime-attributen (respect/
// loyaliteit/verslaving) leven per klant op ACustomerBase; deze rij zet alleen de startwaarden.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "CustomerType.generated.h"

USTRUCT(BlueprintType)
struct FCustomerTypeRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Customer")
	FText DisplayName;

	// Voorkeursproduct (rij-naam in DT_Products). Leeg = willekeurig.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Customer")
	FName PreferredProductId = NAME_None;

	// Seconden geduld in de rij voordat hij boos vertrekt.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Customer")
	float PatienceSeconds = 30.f;

	// Maximaal bedrag dat hij wil betalen (cents) — bod erboven wordt nooit geaccepteerd.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Customer")
	int32 BudgetCents = 2000;

	// Spawn-gewichten (relatief) voor dag resp. nacht. Nacht trekt schichtigere types.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Customer")
	float SpawnWeightDay = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Customer")
	float SpawnWeightNight = 1.f;

	// Start-attributen (0..100).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Customer")
	float StartRespect = 20.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Customer")
	float StartLoyalty = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Customer")
	float StartAddiction = 10.f;
};
