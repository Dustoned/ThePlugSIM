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

// Init-data per hal-schuifpaneel: de retrofitter berekent de ECHTE dicht-positie (de map parkeert ze
// half-open als decor) + hoe ver dit paneel telescopisch moet schuiven.
struct FElevPanelInit
{
	UStaticMeshComponent* Comp = nullptr;
	int32 FloorIdx = 0;
	FVector ClosedPos = FVector::ZeroVector;
	float SlideDist = 0.f;
};

UCLASS()
class WEEDSHOPCORE_API APackElevator : public AActor, public IInteractable
{
	GENERATED_BODY()

public:
	APackElevator();

	// Floors = gesorteerde verdieping-Z's (vloerniveau). InSlideDir = schuifrichting van de panelen
	// (wereld, pocket-kant). Panels = per verdieping (index in Floors) de bestaande schuif-panelen.
	void Setup(const TArray<float>& InFloors, const FVector& InSlideDir, const TArray<FElevPanelInit>& InPanels, const FVector& CabCenterXY, const FVector& OpeningDir);

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
	UPROPERTY() TObjectPtr<UStaticMeshComponent> CabDigit; // verdieping-display IN de cabine
	int32 CabDigitShown = -1;
	// Cabine-schuifdeuren (dubbele deuren: deze rijden met de cabine mee, de hal-panelen blijven per verdieping).
	UPROPERTY() TObjectPtr<UStaticMeshComponent> CabDoorFront;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> CabDoorBack;
	FVector CabDoorFrontBase = FVector::ZeroVector;
	FVector CabDoorBackBase = FVector::ZeroVector;
	float CabSlideSignY = 1.f; // schuifrichting van de hal-deuren omgezet naar cabine-lokale Y

public:
	// Knoppenpaneel in de cabine: per verdieping een knop + cijfer (vast label), naast de opening.
	void BuildCabButtonPanel();
protected:

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
	int32 ShownFloor = 0;      // verdieping op de displays: telt LIVE mee terwijl de cabine rijdt
	float CabZ = 0.f;          // huidige cabine-vloerhoogte
	float DoorOpen = 0.f;      // 0 = dicht, 1 = open (animatie)
	float DwellTimer = 0.f;    // wachttijd met open deuren
	float BoardedTimer = 0.f;  // hoe lang er iemand in de cabine staat
	bool bMoving = false;
	TArray<TWeakObjectPtr<class APackElevatorButton>> Buttons;
	UPROPERTY() TObjectPtr<class UTextRenderComponent> CabArrow; // ^/v naast het cabine-display
	UPROPERTY() TObjectPtr<class UTextRenderComponent> CabDigitText; // zwart cijfer over de witte cabine-plaat
	int32 LastArrowDir = 0;   // -1/0/+1: alleen bij verandering naar de knoppen pushen
	int32 LastGlowFloor = -2; // doel-verdieping die nu gloeit (-1 = geen)
	void UpdateSigns();
};
