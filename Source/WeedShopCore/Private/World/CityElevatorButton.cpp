#include "World/CityElevatorButton.h"

#include "World/CityElevator.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/TextRenderComponent.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/StaticMesh.h"

ACityElevatorButton::ACityElevatorButton()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = false;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	Panel = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Panel"));
	Panel->SetupAttachment(Root);
	if (UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"))) { Panel->SetStaticMesh(Cube); }
	Panel->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics); // wordt door de interact-trace (Visibility) geraakt

	Label = CreateDefaultSubobject<UTextRenderComponent>(TEXT("Label"));
	Label->SetupAttachment(Root);
}

void ACityElevatorButton::Setup(ACityElevator* InElevator, int32 InFloor, bool bInCall)
{
	Elevator = InElevator; Floor = InFloor; bCall = bInCall;

	// Plaatje: dun langs de eigen +X (= de normaal die naar de speler wijst), 14x14cm vlak.
	if (Panel)
	{
		Panel->SetRelativeScale3D(FVector(2.5f, 14.f, 14.f) / 100.f);
		Panel->SetRelativeLocation(FVector::ZeroVector);
		if (UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial")))
		{
			PanelMat = Panel->CreateDynamicMaterialInstance(0, Base);
			if (PanelMat) { PanelMat->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.10f, 0.11f, 0.14f)); }
		}
	}

	// Echt nummer (of "CALL") net vóór het plaatje, leesbaar vanaf de +X-kant.
	if (Label)
	{
		Label->SetText(FText::FromString(bCall ? FString(TEXT("CALL")) : FString::FromInt(Floor + 1)));
		Label->SetWorldSize(bCall ? 5.5f : 9.f);
		Label->SetTextRenderColor(FColor(255, 214, 120));
		Label->SetHorizontalAlignment(EHTA_Center);
		Label->SetVerticalAlignment(EVRTA_TextCenter);
		Label->SetRelativeLocation(FVector(2.0f, 0.f, 0.f));
		Label->SetRelativeRotation(FRotator(0.f, 90.f, 0.f)); // tekstvlak loodrecht op +X, leesbaar naar de speler
	}
}

void ACityElevatorButton::Interact_Implementation(APawn* /*InstigatorPawn*/)
{
	if (Elevator.IsValid()) { Elevator->GoToFloor(Floor); }
	Flash = 1.f;
}

FText ACityElevatorButton::GetInteractionPrompt_Implementation() const
{
	return bCall
		? FText::FromString(FString::Printf(TEXT("Call elevator (floor %d)"), Floor + 1))
		: FText::FromString(FString::Printf(TEXT("Go to floor %d"), Floor + 1));
}

void ACityElevatorButton::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (Flash > 0.f)
	{
		Flash = FMath::Max(0.f, Flash - DeltaSeconds * 2.f);
		if (PanelMat)
		{
			const FLinearColor Base(0.10f, 0.11f, 0.14f), Lit(0.95f, 0.8f, 0.3f);
			PanelMat->SetVectorParameterValue(TEXT("Color"), FMath::Lerp(Base, Lit, Flash));
		}
	}
}
