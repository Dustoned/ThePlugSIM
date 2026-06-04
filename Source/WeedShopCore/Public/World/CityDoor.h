// ACityDoor — een scharnierende deur die automatisch opengaat als een speler dichtbij komt en weer
// dichtgaat als je weg loopt. Lokaal gespawnd door de CityGenerator bij de winkel-/gebouwopeningen.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interaction/Interactable.h"
#include "CityDoor.generated.h"

class UStaticMeshComponent;
class USceneComponent;
class USphereComponent;
class UPrimitiveComponent;

UCLASS()
class WEEDSHOPCORE_API ACityDoor : public AActor, public IInteractable
{
	GENERATED_BODY()

public:
	ACityDoor();

	// Stel de deur in (afmeting + kleur). Hinge zit aan de -X-kant; dicht = paneel langs +X.
	void Setup(float Width, float Height, const FLinearColor& Color);

	// Maak dit een bewoner-deur: op slot voor de speler, met "LOCKED - <naam> lives here".
	void SetResident(const FString& Name) { bLocked = true; bPlayerHome = false; bForSale = false; ResidentName = Name; bOpen = false; }
	bool IsLocked() const { return bLocked; }

	// Jouw eigen woning: open/dicht zoals normaal, prompt "Your home".
	void SetPlayerHome() { bLocked = false; bPlayerHome = true; bForSale = false; ResidentName.Empty(); }
	// Koopbaar pand (nog niet van jou): op slot met "TE KOOP - koop via telefoon".
	void SetForSale() { bLocked = true; bPlayerHome = false; bForSale = true; bOpen = false; ResidentName.Empty(); }

	// Interact (F) opent/sluit de deur.
	virtual void Interact_Implementation(APawn* InstigatorPawn) override;
	virtual FText GetInteractionPrompt_Implementation() const override;

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	// Auto-open: deur zwaait open als iemand ervoor staat (speler of NPC die naar buiten komt).
	UFUNCTION() void OnTriggerBegin(UPrimitiveComponent* Comp, AActor* Other, UPrimitiveComponent* OtherComp, int32 BodyIdx, bool bFromSweep, const FHitResult& Sweep);
	UFUNCTION() void OnTriggerEnd(UPrimitiveComponent* Comp, AActor* Other, UPrimitiveComponent* OtherComp, int32 BodyIdx);

	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> Root;
	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> Hinge;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Panel;
	UPROPERTY(VisibleAnywhere) TObjectPtr<USphereComponent> Trigger; // nabijheid-zone

	int32 NpcNear = 0;      // aantal NPC's in de zone
	int32 OtherNear = 0;    // aantal andere pawns (speler) in de zone
	float CurAngle = 0.f;   // huidige scharnierhoek
	bool bOpen = false;     // open/dicht (getoggled via interact)
	bool bLocked = false;   // bewoner-deur: kan niet door de speler geopend worden
	bool bPlayerHome = false; // jouw eigen woning (open/dicht, prompt "Your home")
	bool bForSale = false;    // koopbaar pand, nog niet van jou
	FString ResidentName;   // naam voor de "LOCKED - ... lives here"-prompt
};
