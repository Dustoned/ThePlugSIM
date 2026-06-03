// ADayNightController — stuurt de scene-belichting op basis van de dag/nacht-klok (GameState
// DayCycle): roteert + dimt de directional light (zon), dimt de SkyLight mee, en zet een paar
// lantaarnpalen neer die automatisch aangaan zodra het donker wordt. Lokaal/cosmetisch (geen
// replicatie nodig: de klok zelf is al gerepliceerd).

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DayNightController.generated.h"

class UDayCycleComponent;
class ADirectionalLight;
class ASkyLight;
class UPointLightComponent;
class UStaticMeshComponent;
class UMaterialInstanceDynamic;

UCLASS()
class WEEDSHOPCORE_API ADayNightController : public AActor
{
	GENERATED_BODY()

public:
	ADayNightController();
	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;

	const UDayCycleComponent* GetDayCycle() const;
	void BuildStreetLamps(const FVector& Center);
	void AddLamp(const FVector& BaseOnGround);
	// Bestaande (felle) binnen-lampen uitzetten en vervangen door een warm, rustig licht.
	void ReplaceIndoorLights();

	UPROPERTY() TObjectPtr<USceneComponent> Root;

	TWeakObjectPtr<ADirectionalLight> Sun;
	TWeakObjectPtr<ASkyLight> Sky;

	UPROPERTY() TArray<TObjectPtr<UPointLightComponent>> LampLights;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> LampHeads;
	UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> LampHeadMats;

	int32 bLampsOn = -1; // -1 = nog niet gezet
};
