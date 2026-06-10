// ADoorRetrofitter - maakt statische deur-bladen in een asset-pack-map (bv. CityBeachStrip) werkend:
// vindt StaticMeshActors met bekende deur-blad-meshes (pivot op het scharnier) en vervangt ze door een
// ACityDoor met datzelfde blad (F = open/dicht, NPC-auto-open, settled-collision). Scant periodiek door
// zodat ook gebouwen die later in-streamen (level instances / world partition) hun deuren krijgen.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DoorRetrofitter.generated.h"

UCLASS()
class WEEDSHOPCORE_API ADoorRetrofitter : public AActor
{
	GENERATED_BODY()

public:
	ADoorRetrofitter();

	// Top-down kaart-render voor pack-maps (MapWidget-fallback zonder CityGenerator).
	class UTextureRenderTarget2D* GetMapRenderTarget() const { return MapRT; }
	FVector2D GetMapCenter() const { return MapCenter; }
	float GetMapOrthoWidth() const { return MapOrtho; }
	void CaptureMapNow();

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	void ScanAndConvert();

	FTimerHandle ScanTimer;
	TSet<TWeakObjectPtr<AActor>> Converted; // originele actors die al een werkende deur kregen
	int32 TotalConverted = 0;
	TArray<TWeakObjectPtr<class ACityDoor>> SpawnedDoors; // om los GLAS aan de juiste deur te hangen
	TSet<TWeakObjectPtr<class UPrimitiveComponent>> GlassFixedComps; // raam-componenten die al geforceerd blokkeren

	// Kaart-capture (lazy aangemaakt bij de eerste CaptureMapNow).
	void EnsureMapCapture();
	UPROPERTY() TObjectPtr<class UTextureRenderTarget2D> MapRT;
	UPROPERTY() TObjectPtr<class USceneCaptureComponent2D> MapCapture;
	FVector2D MapCenter = FVector2D::ZeroVector;
	float MapOrtho = 60000.f;
};
