// APackElevator - werkende lift voor asset-pack-gebouwen (CityBeachStrip). De pack levert per
// verdieping een deurframe + 2 schuif-panelen maar GEEN bewegende cabine; deze actor spawnt de
// SM_ElevatorCabin, rijdt 'm tussen de gescande verdieping-hoogtes en schuift de bestaande
// deur-panelen open op de verdieping waar de cabine staat. Stap in -> deuren dicht -> volgende
// verdieping (wrap bovenaan terug naar beneden), net als de lift in onze eigen stad.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interaction/Interactable.h"
#include "PackElevator.generated.h"

class UStaticMeshComponent;

UCLASS()
class WEEDSHOPCORE_API APackElevator : public AActor, public IInteractable
{
	GENERATED_BODY()

public:
	APackElevator();

	// Floors = gesorteerde verdieping-Z's (vloerniveau). InSlideDir = schuifrichting van de panelen
	// (wereld, pocket-kant). Panels = per verdieping (index in Floors) de bestaande schuif-panelen.
	void Setup(const TArray<float>& InFloors, const FVector& InSlideDir, const TArray<TPair<int32, UStaticMeshComponent*>>& InPanels, const FVector& CabCenterXY, const FVector& OpeningDir);

	// Roep de lift naar deze verdieping (call-knoppen).
	void CallToFloor(int32 FloorIdx);

	// Call-knop koppelen (zodat de digit-bordjes live de cabine-verdieping tonen).
	void RegisterButton(class APackElevatorButton* Btn);

	virtual void Interact_Implementation(APawn* InstigatorPawn) override;
	virtual FText GetInteractionPrompt_Implementation() const override;

protected:
	virtual void Tick(float DeltaSeconds) override;

	bool IsPawnAboard() const;

	UPROPERTY() TObjectPtr<USceneComponent> Root;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Cab;

	struct FPanelRef
	{
		TWeakObjectPtr<UStaticMeshComponent> Comp;
		FVector ClosedPos = FVector::ZeroVector;
		int32 FloorIdx = 0;
		float SlideDist = 0.f; // telescoop: voorste paneel verder dan achterste
	};
	TArray<FPanelRef> Panels;

	TArray<float> Floors;     // vloer-Z per verdieping
	FVector SlideDir = FVector::XAxisVector; // schuifrichting van de panelen (wereld)
	FVector CabXY = FVector::ZeroVector;     // cabine-centrum (XY)

	int32 CurFloor = 0;
	int32 TargetFloor = 0;
	float CabZ = 0.f;          // huidige cabine-vloerhoogte
	float DoorOpen = 0.f;      // 0 = dicht, 1 = open (animatie)
	float DwellTimer = 0.f;    // wachttijd met open deuren
	float BoardedTimer = 0.f;  // hoe lang er iemand in de cabine staat
	bool bMoving = false;
	TArray<TWeakObjectPtr<class APackElevatorButton>> Buttons;
	void UpdateSigns();
};
