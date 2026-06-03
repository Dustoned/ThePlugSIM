// ACityElevator — een werkende liftvloer die tussen verdiepingen beweegt. Stap erop: na een korte
// pauze gaat 'ie een verdieping omhoog (en wrapt bovenaan terug naar beneden), zodat je op elke
// verdieping kunt uitstappen. Leeg en niet beneden -> keert vanzelf terug naar de begane grond.
// Lokaal gespawnd door de CityGenerator (cosmetisch + lokale collision).

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CityElevator.generated.h"

class UStaticMeshComponent;
class USceneComponent;

UCLASS()
class WEEDSHOPCORE_API ACityElevator : public AActor
{
	GENERATED_BODY()

public:
	ACityElevator();

	void Setup(float InBaseZ, float InFloorH, int32 InNumFloors, float FootX, float FootY, const FLinearColor& Color);

protected:
	virtual void Tick(float DeltaSeconds) override;

	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> Root;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Platform;

	float BaseZ = 0.f;
	float FloorH = 330.f;
	int32 NumFloors = 4;
	float FootX = 220.f;
	float FootY = 220.f;

	int32 CurFloor = 0;     // huidige doel-verdieping
	float Dwell = 0.f;      // hoelang stil op deze verdieping
};
