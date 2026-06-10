#include "World/PackElevator.h"

#include "WeedShopCore.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Character.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"

namespace
{
	constexpr float CabSpeed = 240.f;     // cm/s verticaal
	constexpr float DoorSlideTime = 0.7f; // seconden open/dicht
	constexpr float DwellSeconds = 6.f;   // deuren open laten staan zonder passagier
	constexpr float DepartSeconds = 1.6f; // na instappen: zo lang wachten en dan vertrekken
}

APackElevator::APackElevator()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = false; // lokaal/cosmetisch, net als ACityElevator/ACityDoor

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	Cab = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Cab"));
	Cab->SetupAttachment(Root);
	Cab->SetMobility(EComponentMobility::Movable);
	Cab->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Cab->SetCanEverAffectNavigation(false);
	if (UStaticMesh* CabMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/CityBeachStrip/Meshes/Architecture/Interiors/Elevator/SM_ElevatorCabin.SM_ElevatorCabin")))
	{
		Cab->SetStaticMesh(CabMesh);
	}
}

void APackElevator::Setup(const TArray<float>& InFloors, const FVector& InSlideDir, const TArray<TPair<int32, UStaticMeshComponent*>>& InPanels, const FVector& CabCenterXY)
{
	Floors = InFloors;
	Floors.Sort();
	CabXY = FVector(CabCenterXY.X, CabCenterXY.Y, 0.f);
	SlideDir = InSlideDir;
	SlideDir.Z = 0.f;
	SlideDir.Normalize();

	Panels.Reset();
	for (const TPair<int32, UStaticMeshComponent*>& P : InPanels)
	{
		if (!P.Value) { continue; }
		FPanelRef R;
		R.Comp = P.Value;
		R.ClosedPos = P.Value->GetComponentLocation();
		R.FloorIdx = P.Key;
		Panels.Add(R);
	}
	// Telescoop: per verdieping schuift het paneel dat het dichtst bij de schuifrichting-kant zit het
	// verst (130), het andere de helft (65) - dan stapelen ze netjes achter elkaar.
	for (int32 FloorIdx = 0; FloorIdx < Floors.Num(); ++FloorIdx)
	{
		FPanelRef* A = nullptr; FPanelRef* B = nullptr;
		for (FPanelRef& R : Panels)
		{
			if (R.FloorIdx != FloorIdx) { continue; }
			if (!A) { A = &R; } else { B = &R; }
		}
		if (A && B)
		{
			// SlideDir wijst naar de pocket-kant: het paneel dat daar al het dichtst bij zit (hoogste dot)
			// is het ACHTERSTE (schuift 66), het voorste schuift er volledig achter (132).
			const float DotA = FVector::DotProduct(A->ClosedPos, SlideDir);
			const float DotB = FVector::DotProduct(B->ClosedPos, SlideDir);
			FPanelRef* Back  = (DotA > DotB) ? A : B;
			FPanelRef* Front = (DotA > DotB) ? B : A;
			Front->SlideDist = 132.f;
			Back->SlideDist = 66.f;
		}
		else if (A) { A->SlideDist = 132.f; }
	}

	CurFloor = 0;
	TargetFloor = 0;
	CabZ = Floors.Num() ? Floors[0] : 0.f;
	DoorOpen = 1.f; // begane grond: deuren open, klaar om in te stappen
	DwellTimer = DwellSeconds;
	SetActorLocation(FVector(CabXY.X, CabXY.Y, CabZ));
	if (Cab) { Cab->SetRelativeLocation(FVector::ZeroVector); }
}

bool APackElevator::IsPawnAboard() const
{
	UWorld* W = GetWorld();
	if (!W) { return false; }
	for (FConstPlayerControllerIterator It = W->GetPlayerControllerIterator(); It; ++It)
	{
		const APawn* P = It->Get() ? It->Get()->GetPawn() : nullptr;
		if (!P) { continue; }
		const FVector L = P->GetActorLocation();
		if (FVector::Dist2D(L, GetActorLocation()) < 130.f && FMath::Abs(L.Z - (CabZ + 100.f)) < 160.f)
		{
			return true;
		}
	}
	return false;
}

void APackElevator::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (Floors.Num() < 2) { return; }

	const float TargetZ = Floors[TargetFloor];
	bMoving = !FMath::IsNearlyEqual(CabZ, TargetZ, 1.f);

	// Deuren: open op een verdieping met stilstaande cabine, dicht tijdens het rijden.
	const bool bWantOpen = !bMoving;
	DoorOpen = FMath::FInterpConstantTo(DoorOpen, bWantOpen ? 1.f : 0.f, DeltaSeconds, 1.f / DoorSlideTime);
	for (const FPanelRef& R : Panels)
	{
		UStaticMeshComponent* C = R.Comp.Get();
		if (!C) { continue; }
		// Alleen de panelen van de verdieping waar de cabine is (of vertrekt) bewegen mee; rest dicht.
		const bool bThisFloor = (R.FloorIdx == CurFloor);
		const float Amount = bThisFloor ? DoorOpen : 0.f;
		C->SetWorldLocation(R.ClosedPos + SlideDir * (R.SlideDist * Amount));
	}

	if (bMoving)
	{
		// Pas rijden als de deuren dicht zijn.
		if (DoorOpen > 0.02f) { return; }
		CabZ = FMath::FInterpConstantTo(CabZ, TargetZ, DeltaSeconds, CabSpeed);
		SetActorLocation(FVector(CabXY.X, CabXY.Y, CabZ));
		if (FMath::IsNearlyEqual(CabZ, TargetZ, 1.f))
		{
			CabZ = TargetZ;
			CurFloor = TargetFloor;
			DwellTimer = DwellSeconds;
			BoardedTimer = 0.f;
		}
		return;
	}

	// Stilstaand met open(ende) deuren: passagier aan boord -> na korte wacht naar de volgende verdieping.
	if (IsPawnAboard())
	{
		BoardedTimer += DeltaSeconds;
		if (BoardedTimer >= DepartSeconds)
		{
			TargetFloor = (CurFloor + 1) % Floors.Num(); // wrap: bovenste -> begane grond
			BoardedTimer = 0.f;
		}
	}
	else
	{
		BoardedTimer = 0.f;
		// Leeg en niet beneden? Na de dwell-tijd terug naar de begane grond.
		DwellTimer -= DeltaSeconds;
		if (DwellTimer <= 0.f && CurFloor != 0)
		{
			TargetFloor = 0;
		}
	}
}

void APackElevator::Interact_Implementation(APawn* InstigatorPawn)
{
	// F = direct vertrekken naar de volgende verdieping (zonder de instap-wachttijd af te wachten).
	if (!bMoving && Floors.Num() >= 2)
	{
		TargetFloor = (CurFloor + 1) % Floors.Num();
	}
}

FText APackElevator::GetInteractionPrompt_Implementation() const
{
	if (bMoving) { return FText::FromString(TEXT("Elevator moving...")); }
	return FText::FromString(FString::Printf(TEXT("Elevator - floor %d/%d (step in, or F to go)"), CurFloor + 1, Floors.Num()));
}
