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

void APackElevatorButton::Interact_Implementation(APawn* InstigatorPawn)
{
	if (APackElevator* E = Elevator.Get()) { E->CallToFloor(FloorIdx); }
}

FText APackElevatorButton::GetInteractionPrompt_Implementation() const
{
	return FText::FromString(TEXT("Call elevator"));
}
