// ACityDoor — een scharnierende deur die automatisch opengaat als een speler dichtbij komt en weer
// dichtgaat als je weg loopt. Lokaal gespawnd door de CityGenerator bij de winkel-/gebouwopeningen.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interaction/Interactable.h"
#include "CityDoor.generated.h"

class UStaticMeshComponent;
class USceneComponent;

UCLASS()
class WEEDSHOPCORE_API ACityDoor : public AActor, public IInteractable
{
	GENERATED_BODY()

public:
	ACityDoor();

	// Stel de deur in (afmeting + kleur). Hinge zit aan de -X-kant; dicht = paneel langs +X.
	void Setup(float Width, float Height, const FLinearColor& Color);

	// Maak dit een bewoner-deur: op slot voor de speler, met "LOCKED - <naam> lives here".
	void SetResident(const FString& Name) { bLocked = true; ResidentName = Name; bOpen = false; }
	bool IsLocked() const { return bLocked; }

	// Interact (F) opent/sluit de deur.
	virtual void Interact_Implementation(APawn* InstigatorPawn) override;
	virtual FText GetInteractionPrompt_Implementation() const override;

protected:
	virtual void Tick(float DeltaSeconds) override;

	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> Root;
	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> Hinge;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Panel;

	float CurAngle = 0.f;   // huidige scharnierhoek
	bool bOpen = false;     // open/dicht (getoggled via interact)
	bool bLocked = false;   // bewoner-deur: kan niet door de speler geopend worden
	FString ResidentName;   // naam voor de "LOCKED - ... lives here"-prompt
};
