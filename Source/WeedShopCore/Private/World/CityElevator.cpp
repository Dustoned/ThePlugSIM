#include "World/CityElevator.h"

#include "World/CityElevatorButton.h"
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
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	auto Tint = [&](UStaticMeshComponent* C, const FLinearColor& Col) { if (C && Base) { if (UMaterialInstanceDynamic* M = C->CreateDynamicMaterialInstance(0, Base)) { M->SetVectorParameterValue(TEXT("Color"), Col); } } };

	const float CabH = 240.f, T = 10.f;
	const FLinearColor Cab = Color * 1.15f;
	const FLinearColor DoorCol(0.16f, 0.17f, 0.19f);

	if (Platform)
	{
		Platform->SetRelativeScale3D(FVector(FootX, FootY, 7.f) / 100.f); // dunner platform
		Platform->SetRelativeLocation(FVector(0.f, 0.f, 3.5f));            // bovenkant ~vloerniveau
		Tint(Platform, Color);
	}

	auto Wall = [&](const TCHAR* Name, const FVector& Loc, const FVector& SizeCm, const FLinearColor& Col)
	{
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this, Name);
		C->RegisterComponent();
		// Expliciet aan Root vasthaken NA registratie. SetupAttachment alleen is voor runtime-componenten
		// niet betrouwbaar -> de cabinewanden bleven in de wereld staan i.p.v. mee te bewegen met de lift.
		C->AttachToComponent(Root, FAttachmentTransformRules::KeepRelativeTransform);
		if (Cube) { C->SetStaticMesh(Cube); }
		C->SetRelativeLocation(Loc); C->SetRelativeScale3D(SizeCm / 100.f);
		C->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Tint(C, Col);
		return C;
	};

	// Cabine: achterwand (-Y), 2 zijwanden (±X), plafond.
	Wall(TEXT("CabBack"),  FVector(0.f, -FootY * 0.5f, 6.f + CabH * 0.5f), FVector(FootX + T, T, CabH), Cab);
	Wall(TEXT("CabLeft"),  FVector(-FootX * 0.5f, 0.f, 6.f + CabH * 0.5f), FVector(T, FootY, CabH), Cab);
	Wall(TEXT("CabRight"), FVector( FootX * 0.5f, 0.f, 6.f + CabH * 0.5f), FVector(T, FootY, CabH), Cab);
	Wall(TEXT("CabCeil"),  FVector(0.f, 0.f, 6.f + CabH), FVector(FootX + T, FootY + T, T), Cab * 0.8f);

	// --- Deur-opmaat: bi-parting bladen die naar de zijkanten in de "pockets" schuiven ---
	const float DoorH = 210.f;
	OpenW = FMath::Clamp((FootX - 80.f) * 0.5f, 40.f, FootX * 0.24f); // halve opening; bladen passen in de pocket (geen muur-glitch)
	DoorZ = 6.f + DoorH * 0.5f;
	const float LeafW = OpenW;            // elk blad dekt de halve opening
	const float FrontY = FootY * 0.5f;    // voorvlak van de cabine
	const float PocketW = FootX * 0.5f - OpenW; // wand-stukje naast de opening (pocket/kozijn)

	// Voor-kozijn: pilaartjes naast de opening + latei erboven, zodat de voorkant dicht is rond de deur.
	if (PocketW > 2.f)
	{
		Wall(TEXT("CabFrontL"), FVector(-(OpenW + PocketW * 0.5f), FrontY, 6.f + DoorH * 0.5f), FVector(PocketW, T, DoorH), Cab);
		Wall(TEXT("CabFrontR"), FVector( (OpenW + PocketW * 0.5f), FrontY, 6.f + DoorH * 0.5f), FVector(PocketW, T, DoorH), Cab);
	}
	Wall(TEXT("CabFrontTop"), FVector(0.f, FrontY, 6.f + DoorH + (CabH - DoorH) * 0.5f), FVector(FootX + T, T, CabH - DoorH), Cab);

	// Cabinedeur-bladen (bewegen mee met de cabine, kinderen van Root).
	CabDoorL = Wall(TEXT("CabDoorL"), FVector(-LeafW * 0.5f, FrontY, DoorZ), FVector(LeafW, T, DoorH), DoorCol);
	CabDoorR = Wall(TEXT("CabDoorR"), FVector( LeafW * 0.5f, FrontY, DoorZ), FVector(LeafW, T, DoorH), DoorCol);

	// Schachtdeuren per verdieping (staan op hun eigen vloer; wereld-Z wordt elke tick vastgehouden).
	LandDoorL.Reset(); LandDoorR.Reset(); CurLand.Reset();
	for (int32 f = 0; f < NumFloors; ++f)
	{
		UStaticMeshComponent* L = Wall(*FString::Printf(TEXT("LandL_%d"), f), FVector(-LeafW * 0.5f, FrontY + T + 2.f, DoorZ), FVector(LeafW, T, DoorH), DoorCol * 0.92f);
		UStaticMeshComponent* R = Wall(*FString::Printf(TEXT("LandR_%d"), f), FVector( LeafW * 0.5f, FrontY + T + 2.f, DoorZ), FVector(LeafW, T, DoorH), DoorCol * 0.92f);
		LandDoorL.Add(L); LandDoorR.Add(R); CurLand.Add(0.f);
	}

	SetActorLocation(FVector(GetActorLocation().X, GetActorLocation().Y, BaseZ));

	// --- Knoppen: verdieping-keuze in de cabine + oproepknop op elke verdieping ---
	UWorld* W = GetWorld();
	if (W)
	{
		const FTransform XF = GetActorTransform(); // basis op BaseZ + yaw van de cabine
		const float ButZ = 130.f;                   // midden van het paneel, op handhoogte
		FActorSpawnParameters BSP; BSP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

		// Cabine-keuzeknoppen: COMPACT bedieningspaneel op de rechter-zijwand (lokaal +X), kijkend de
		// cabine in (-X). Rooster van 3 kolommen met kleine spacing i.p.v. ver uit elkaar.
		const int32 Cols = 3;
		const float ColSp = 22.f, RowSp = 22.f;
		const int32 NRows = (NumFloors + Cols - 1) / Cols;
		for (int32 f = 0; f < NumFloors; ++f)
		{
			const int32 col = f % Cols;
			const int32 row = f / Cols;
			const float ly = (col - (Cols - 1) * 0.5f) * ColSp;
			const float lz = ButZ + ((NRows - 1) * 0.5f - row) * RowSp; // verdieping 1 onderaan
			const FVector RelLoc(FootX * 0.5f - 4.f, ly, lz);
			if (ACityElevatorButton* B = W->SpawnActor<ACityElevatorButton>(ACityElevatorButton::StaticClass(), FTransform(), BSP))
			{
				B->AttachToActor(this, FAttachmentTransformRules::KeepRelativeTransform);
				B->SetActorRelativeLocation(RelLoc);
				B->SetActorRelativeRotation(FRotator(0.f, 180.f, 0.f)); // +X -> cabine-binnenkant (-X)
				B->Setup(this, f, false);
			}
		}

		// Oproepknoppen: vast op elke verdieping, naast de lift-opening, kijkend de gang in (+Y).
		const FVector LocalCall(OpenW + 34.f, FootY * 0.5f + 12.f, 0.f);
		for (int32 f = 0; f < NumFloors; ++f)
		{
			const FVector WLoc = XF.TransformPosition(LocalCall + FVector(0.f, 0.f, f * FloorH + ButZ));
			const FRotator WRot(0.f, XF.GetRotation().Rotator().Yaw + 90.f, 0.f); // +X -> +Y (de gang in)
			if (ACityElevatorButton* B = W->SpawnActor<ACityElevatorButton>(ACityElevatorButton::StaticClass(), FTransform(WRot, WLoc), BSP))
			{
				B->Setup(this, f, true);
			}
		}
	}
}

void ACityElevator::GoToFloor(int32 F)
{
	CurFloor = FMath::Clamp(F, 0, NumFloors - 1);
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

	const FVector Loc = GetActorLocation();
	const float TargetZ = BaseZ + CurFloor * FloorH;
	const bool bAtFloor = FMath::IsNearlyEqual(Loc.Z, TargetZ, 4.f); // cabine staat stil op een verdieping

	// Cabinedeur: bi-parting bladen schuiven naar de zijkanten (blijven binnen de cabine, nooit buiten zicht).
	// Trager (4.0) zodat ze duidelijk bewegen i.p.v. meteen weg te klappen.
	const float CabTarget = (bDoorOpen && bAtFloor) ? OpenW : 0.f;
	CurDoor = FMath::FInterpTo(CurDoor, CabTarget, DeltaSeconds, 4.0f);
	if (CabDoorL) { CabDoorL->SetRelativeLocation(FVector(-OpenW * 0.5f - CurDoor, FootY * 0.5f, DoorZ)); CabDoorL->SetCollisionResponseToChannel(ECC_Pawn, (CurDoor < OpenW * 0.6f) ? ECR_Block : ECR_Ignore); }
	if (CabDoorR) { CabDoorR->SetRelativeLocation(FVector( OpenW * 0.5f + CurDoor, FootY * 0.5f, DoorZ)); CabDoorR->SetCollisionResponseToChannel(ECC_Pawn, (CurDoor < OpenW * 0.6f) ? ECR_Block : ECR_Ignore); }

	// Schachtdeuren: blijven op hun verdieping (wereld-Z vastgehouden door relatieve Z te compenseren).
	// Open alleen als de cabine OP die verdieping staat en de speler dichtbij is; anders dicht -> geen val in de schacht.
	for (int32 f = 0; f < LandDoorL.Num(); ++f)
	{
		const float FloorZ = BaseZ + f * FloorH;
		const bool bCabHere = FMath::IsNearlyEqual(Loc.Z, FloorZ, 6.f);
		const float Target = (bDoorOpen && bCabHere) ? OpenW : 0.f;
		CurLand[f] = FMath::FInterpTo(CurLand[f], Target, DeltaSeconds, 4.0f);
		const float RelZ = (FloorZ - Loc.Z) + DoorZ; // compenseer de cabine-beweging -> wereld-Z blijft op de vloer
		const bool bBlock = CurLand[f] < OpenW * 0.6f;
		if (UStaticMeshComponent* L = LandDoorL[f]) { L->SetRelativeLocation(FVector(-OpenW * 0.5f - CurLand[f], FootY * 0.5f + 12.f, RelZ)); L->SetCollisionResponseToChannel(ECC_Pawn, bBlock ? ECR_Block : ECR_Ignore); }
		if (UStaticMeshComponent* R = LandDoorR[f]) { R->SetRelativeLocation(FVector( OpenW * 0.5f + CurLand[f], FootY * 0.5f + 12.f, RelZ)); R->SetCollisionResponseToChannel(ECC_Pawn, bBlock ? ECR_Block : ECR_Ignore); }
	}

	// Beweeg naar de gevraagde verdieping (gezet door de knoppen). Blijft daar tot je een andere kiest/roept.
	const float NewZ = FMath::FInterpConstantTo(Loc.Z, TargetZ, DeltaSeconds, 220.f);
	SetActorLocation(FVector(Loc.X, Loc.Y, NewZ));
}
