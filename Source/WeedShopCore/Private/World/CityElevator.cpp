#include "World/CityElevator.h"

#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/StaticMesh.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/Pawn.h"

ACityElevator::ACityElevator()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = false;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	Platform = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Platform"));
	Platform->SetupAttachment(Root);
	if (UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"))) { Platform->SetStaticMesh(Cube); }
	Platform->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
}

void ACityElevator::Setup(float InBaseZ, float InFloorH, int32 InNumFloors, float InFootX, float InFootY, const FLinearColor& Color)
{
	BaseZ = InBaseZ; FloorH = InFloorH; NumFloors = FMath::Max(1, InNumFloors);
	FootX = InFootX; FootY = InFootY;
	if (Platform)
	{
		Platform->SetRelativeScale3D(FVector(FootX, FootY, 12.f) / 100.f);
		Platform->SetRelativeLocation(FVector(0.f, 0.f, 6.f));
		if (UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial")))
		{
			if (UMaterialInstanceDynamic* M = Platform->CreateDynamicMaterialInstance(0, Base)) { M->SetVectorParameterValue(TEXT("Color"), Color); }
		}
	}
	SetActorLocation(FVector(GetActorLocation().X, GetActorLocation().Y, BaseZ));
}

void ACityElevator::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	const FVector Loc = GetActorLocation();
	const float TargetZ = BaseZ + CurFloor * FloorH;

	// Beweeg naar de doel-verdieping.
	const float NewZ = FMath::FInterpConstantTo(Loc.Z, TargetZ, DeltaSeconds, 220.f);
	SetActorLocation(FVector(Loc.X, Loc.Y, NewZ));
	const bool bArrived = FMath::IsNearlyEqual(NewZ, TargetZ, 2.f);

	// Staat iemand op de lift?
	bool bOccupied = false;
	if (const APawn* P = UGameplayStatics::GetPlayerPawn(this, 0))
	{
		const FVector PL = P->GetActorLocation();
		if (FMath::Abs(PL.X - Loc.X) < FootX * 0.5f + 10.f &&
			FMath::Abs(PL.Y - Loc.Y) < FootY * 0.5f + 10.f &&
			PL.Z > NewZ && PL.Z < NewZ + 260.f)
		{
			bOccupied = true;
		}
	}

	if (!bArrived) { Dwell = 0.f; return; }

	Dwell += DeltaSeconds;
	if (bOccupied)
	{
		// Na een korte pauze een verdieping omhoog (bovenaan -> terug naar beneden), zodat je kunt uitstappen.
		if (Dwell > 1.6f)
		{
			CurFloor = (CurFloor + 1) % NumFloors;
			Dwell = 0.f;
		}
	}
	else if (CurFloor != 0 && Dwell > 4.f)
	{
		CurFloor = 0; // leeg -> terug naar begane grond
		Dwell = 0.f;
	}
}
