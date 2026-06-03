#include "World/CityDoor.h"

#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/StaticMesh.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/Pawn.h"

ACityDoor::ACityDoor()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = false; // lokaal (cosmetisch + lokale collision); ieder z'n eigen deur

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	Hinge = CreateDefaultSubobject<USceneComponent>(TEXT("Hinge"));
	Hinge->SetupAttachment(Root);

	Panel = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Panel"));
	Panel->SetupAttachment(Hinge);
	if (UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"))) { Panel->SetStaticMesh(Cube); }
	Panel->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
}

void ACityDoor::Setup(float Width, float Height, const FLinearColor& Color)
{
	if (!Panel) { return; }
	// Scharnier aan de -X-kant: paneel loopt van de hinge (X=0) naar +X.
	Panel->SetRelativeScale3D(FVector(Width, 8.f, Height) / 100.f);
	Panel->SetRelativeLocation(FVector(Width * 0.5f, 0.f, Height * 0.5f));
	if (UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial")))
	{
		if (UMaterialInstanceDynamic* M = Panel->CreateDynamicMaterialInstance(0, Base)) { M->SetVectorParameterValue(TEXT("Color"), Color); }
	}
}

void ACityDoor::Interact_Implementation(APawn* InstigatorPawn)
{
	bOpen = !bOpen; // F = open/dicht
}

FText ACityDoor::GetInteractionPrompt_Implementation() const
{
	return bOpen ? FText::FromString(TEXT("Close door")) : FText::FromString(TEXT("Open door"));
}

void ACityDoor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const float Target = bOpen ? -95.f : 0.f;
	CurAngle = FMath::FInterpTo(CurAngle, Target, DeltaSeconds, 7.f);
	if (Hinge) { Hinge->SetRelativeRotation(FRotator(0.f, CurAngle, 0.f)); }
}
