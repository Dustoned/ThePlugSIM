// ACityDoor — een scharnierende deur die automatisch opengaat als een speler dichtbij komt en weer
// dichtgaat als je weg loopt. Lokaal gespawnd door de CityGenerator bij de winkel-/gebouwopeningen.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CityDoor.generated.h"

class UStaticMeshComponent;
class USceneComponent;

UCLASS()
class WEEDSHOPCORE_API ACityDoor : public AActor
{
	GENERATED_BODY()

public:
	ACityDoor();

	// Stel de deur in (afmeting + kleur). Hinge zit aan de -X-kant; dicht = paneel langs +X.
	void Setup(float Width, float Height, const FLinearColor& Color);

protected:
	virtual void Tick(float DeltaSeconds) override;

	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> Root;
	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> Hinge;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Panel;

	float CurAngle = 0.f;   // huidige scharnierhoek
	float OpenDist = 280.f; // binnen deze afstand gaat de deur open
};
