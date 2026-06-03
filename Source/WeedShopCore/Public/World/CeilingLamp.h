// ACeilingLamp — klein plafond-/spotlamp-model met een warme neerwaartse spot. Plaatsbaar (koop bij
// de furniture store) en oppakbaar (G). Het licht staat altijd aan (binnenverlichting).

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CeilingLamp.generated.h"

class UStaticMeshComponent;
class UPointLightComponent;

UCLASS()
class WEEDSHOPCORE_API ACeilingLamp : public AActor
{
	GENERATED_BODY()

public:
	ACeilingLamp();

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> Root;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Shade; // kapje (kegel)
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Bulb;  // lampje
	UPROPERTY(VisibleAnywhere) TObjectPtr<UPointLightComponent> Light; // warm omni-licht
};
