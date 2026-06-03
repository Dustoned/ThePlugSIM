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

	DoorPanel = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DoorPanel"));
	DoorPanel->SetupAttachment(Root);
	if (UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"))) { DoorPanel->SetStaticMesh(Cube); }
	DoorPanel->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
}

void ACityElevator::Setup(float InBaseZ, float InFloorH, int32 InNumFloors, float InFootX, float InFootY, const FLinearColor& Color)
{
	BaseZ = InBaseZ; FloorH = InFloorH; NumFloors = FMath::Max(1, InNumFloors);
	FootX = InFootX; FootY = InFootY;
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	auto Tint = [&](UStaticMeshComponent* C, const FLinearColor& Col) { if (C && Base) { if (UMaterialInstanceDynamic* M = C->CreateDynamicMaterialInstance(0, Base)) { M->SetVectorParameterValue(TEXT("Color"), Col); } } };

	const float CabH = 240.f, T = 10.f;
	const FLinearColor Cab = Color * 1.15f;

	if (Platform)
	{
		Platform->SetRelativeScale3D(FVector(FootX, FootY, 12.f) / 100.f);
		Platform->SetRelativeLocation(FVector(0.f, 0.f, 6.f));
		Tint(Platform, Color);
	}

	// Cabine: achterwand (-Y), 2 zijwanden (±X), plafond. Bewegen mee met de lift (kinderen van Root).
	auto Wall = [&](const TCHAR* Name, const FVector& Loc, const FVector& SizeCm, const FLinearColor& Col)
	{
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this, Name);
		C->SetupAttachment(Root); C->RegisterComponent();
		if (Cube) { C->SetStaticMesh(Cube); }
		C->SetRelativeLocation(Loc); C->SetRelativeScale3D(SizeCm / 100.f);
		C->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Tint(C, Col);
	};
	Wall(TEXT("CabBack"),  FVector(0.f, -FootY * 0.5f, 6.f + CabH * 0.5f), FVector(FootX + T, T, CabH), Cab);
	Wall(TEXT("CabLeft"),  FVector(-FootX * 0.5f, 0.f, 6.f + CabH * 0.5f), FVector(T, FootY, CabH), Cab);
	Wall(TEXT("CabRight"), FVector( FootX * 0.5f, 0.f, 6.f + CabH * 0.5f), FVector(T, FootY, CabH), Cab);
	Wall(TEXT("CabCeil"),  FVector(0.f, 0.f, 6.f + CabH), FVector(FootX + T, FootY + T, T), Cab * 0.8f);

	// Schuifdeur op de voorkant (+Y), schuift langs +X open.
	DoorSlide = FootX * 0.92f;
	if (DoorPanel)
	{
		DoorPanel->SetRelativeScale3D(FVector(FootX * 0.92f, T, 210.f) / 100.f);
		DoorPanel->SetRelativeLocation(FVector(0.f, FootY * 0.5f, 6.f + 105.f));
		Tint(DoorPanel, FLinearColor(0.16f, 0.17f, 0.19f));
	}

	SetActorLocation(FVector(GetActorLocation().X, GetActorLocation().Y, BaseZ));
}

void ACityElevator::Interact_Implementation(APawn* InstigatorPawn)
{
	bDoorOpen = !bDoorOpen; // F = schuifdeur open/dicht
}

FText ACityElevator::GetInteractionPrompt_Implementation() const
{
	return bDoorOpen ? FText::FromString(TEXT("Close elevator")) : FText::FromString(TEXT("Open elevator"));
}

void ACityElevator::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// Lift-schuifdeur gaat AUTOMATISCH open als de speler dichtbij de cabine staat (zoals een echte lift),
	// zodat je naar binnen kunt zonder te interacten.
	{
		const FVector Loc0 = GetActorLocation();
		bDoorOpen = false;
		if (const APawn* P = UGameplayStatics::GetPlayerPawn(this, 0))
		{
			const FVector PL = P->GetActorLocation();
			const float Horiz = FVector::Dist2D(PL, Loc0);
			if (Horiz < FMath::Max(FootX, FootY) * 0.5f + 200.f && PL.Z > Loc0.Z - 120.f && PL.Z < Loc0.Z + 320.f)
			{
				bDoorOpen = true;
			}
		}
	}

	// Schuifdeur animeren (open = naar +X geschoven). Blokkeert de pawn alleen als 'ie dicht is.
	const float DoorTarget = bDoorOpen ? DoorSlide : 0.f;
	CurDoor = FMath::FInterpTo(CurDoor, DoorTarget, DeltaSeconds, 6.f);
	if (DoorPanel)
	{
		DoorPanel->SetRelativeLocation(FVector(CurDoor, FootY * 0.5f, 6.f + 105.f));
		DoorPanel->SetCollisionResponseToChannel(ECC_Pawn, (CurDoor < 5.f) ? ECR_Block : ECR_Ignore);
	}

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
