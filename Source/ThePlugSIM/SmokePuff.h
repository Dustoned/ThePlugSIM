// ASmokePuff — kort, puur cosmetisch rookwolkje (groeit, stijgt en fade-out) dat uit je hoofd komt
// als je een joint oprookt. Geen replicatie nodig: lokaal gespawnd op elke client.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SmokePuff.generated.h"

class UStaticMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;

UCLASS()
class ASmokePuff : public AActor
{
	GENERATED_BODY()

public:
	ASmokePuff();
	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Mesh;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> SmokeMat;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> MID;

	float Life = 0.f;
	float MaxLife = 1.6f;
	float StartScale = 0.12f;
	float EndScale = 0.9f;
};
