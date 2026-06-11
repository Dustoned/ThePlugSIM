#include "World/PackElevatorButton.h"

#include "World/PackElevator.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"

APackElevatorButton::APackElevatorButton()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	SetRootComponent(Mesh);
	Mesh->SetMobility(EComponentMobility::Movable);
	Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Mesh->SetCanEverAffectNavigation(false);
	if (UStaticMesh* M = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/CityBeachStrip/Meshes/Architecture/Interiors/Elevator/SM_ElevatorCallButton01.SM_ElevatorCallButton01")))
	{
		Mesh->SetStaticMesh(M);
	}
}

void APackElevatorButton::Setup(APackElevator* InElevator, int32 InFloorIdx)
{
	Elevator = InElevator;
	FloorIdx = InFloorIdx;
}

void APackElevatorButton::SetupSign(const FVector& SignWorldLoc, const FRotator& SignRot, float Scale)
{
	if (!DigitMesh)
	{
		DigitMesh = NewObject<UStaticMeshComponent>(this);
		DigitMesh->SetupAttachment(GetRootComponent());
		DigitMesh->RegisterComponent();
		DigitMesh->SetMobility(EComponentMobility::Movable);
		DigitMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		DigitMesh->SetCanEverAffectNavigation(false);
	}
	DigitMesh->SetWorldLocationAndRotation(SignWorldLoc, SignRot);
	DigitMesh->SetWorldScale3D(FVector(Scale)); // digit-mesh is 3x5cm -> opschalen naar leesbaar formaat
}

void APackElevatorButton::SetDigit(int32 Digit)
{
	if (!DigitMesh || Digit == CurDigit) { return; }
	CurDigit = Digit;
	const FString Path = FString::Printf(TEXT("/Game/CityBeachStrip/Meshes/Architecture/Interiors/Elevator/SM_ElevatorNumber_%d.SM_ElevatorNumber_%d"),
		FMath::Clamp(Digit, 0, 9), FMath::Clamp(Digit, 0, 9));
	if (UStaticMesh* M = LoadObject<UStaticMesh>(nullptr, *Path))
	{
		DigitMesh->SetStaticMesh(M);
	}
}

void APackElevatorButton::Interact_Implementation(APawn* InstigatorPawn)
{
	if (APackElevator* E = Elevator.Get()) { E->CallToFloor(FloorIdx); }
}

void APackElevatorButton::SetCabMode()
{
	bCabMode = true;
}

FText APackElevatorButton::GetInteractionPrompt_Implementation() const
{
	if (bCabMode)
	{
		return FText::FromString(FloorIdx == 0
			? FString(TEXT("Go to ground floor (0)"))
			: FString::Printf(TEXT("Go to floor %d"), FloorIdx));
	}
	return FText::FromString(TEXT("Call elevator"));
}
