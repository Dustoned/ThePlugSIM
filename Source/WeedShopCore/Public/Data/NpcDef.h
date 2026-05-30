// NPC-definitie voor DT_NPCs. Elke rij = één persoon met een vaste naam + begin-stats.
// Schaalt naar ~100 door rijen toe te voegen. Runtime-stats leven in UNpcRegistryComponent
// (persistent per persoon); deze rij zet alleen de startwaarden.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "NpcDef.generated.h"

USTRUCT(BlueprintType)
struct FNpcDef : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC")
	float BaseRespect = 15.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC")
	float BaseLoyalty = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC")
	float BaseAddiction = 10.f;
};
