#include "World/CityDoor.h"

#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SphereComponent.h"
#include "Customer/CustomerBase.h"
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

	// Nabijheid-zone: detecteert pawns die voor de deur staan (auto-open).
	Trigger = CreateDefaultSubobject<USphereComponent>(TEXT("Trigger"));
	Trigger->SetupAttachment(Root);
	Trigger->InitSphereRadius(150.f);
	Trigger->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Trigger->SetCollisionResponseToAllChannels(ECR_Ignore);
	Trigger->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	Trigger->SetGenerateOverlapEvents(true);
}

void ACityDoor::BeginPlay()
{
	Super::BeginPlay();
	if (Trigger)
	{
		Trigger->OnComponentBeginOverlap.AddDynamic(this, &ACityDoor::OnTriggerBegin);
		Trigger->OnComponentEndOverlap.AddDynamic(this, &ACityDoor::OnTriggerEnd);
	}
}

void ACityDoor::OnTriggerBegin(UPrimitiveComponent*, AActor* Other, UPrimitiveComponent*, int32, bool, const FHitResult&)
{
	if (Cast<ACustomerBase>(Other)) { ++NpcNear; }
	else if (Cast<APawn>(Other))    { ++OtherNear; }
}

void ACityDoor::OnTriggerEnd(UPrimitiveComponent*, AActor* Other, UPrimitiveComponent*, int32)
{
	if (Cast<ACustomerBase>(Other)) { NpcNear = FMath::Max(0, NpcNear - 1); }
	else if (Cast<APawn>(Other))    { OtherNear = FMath::Max(0, OtherNear - 1); }
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
	// Zet de nabijheid-zone midden voor de deuropening (dekt beide kanten van de deur).
	if (Trigger)
	{
		Trigger->SetRelativeLocation(FVector(Width * 0.5f, 0.f, Height * 0.5f));
		Trigger->SetSphereRadius(FMath::Max(130.f, Width * 0.6f));
	}
}

void ACityDoor::Interact_Implementation(APawn* InstigatorPawn)
{
	if (bLocked) { return; } // bewoner-deur: op slot voor de speler
	bOpen = !bOpen; // F = open/dicht
}

FText ACityDoor::GetInteractionPrompt_Implementation() const
{
	if (bLocked)
	{
		if (bForSale) { return FText::FromString(TEXT("TE KOOP - koop via telefoon (Upgrades)")); }
		return FText::FromString(ResidentName.IsEmpty()
			? FString(TEXT("LOCKED"))
			: FString::Printf(TEXT("LOCKED - %s lives here"), *ResidentName));
	}
	if (bPlayerHome) { return bOpen ? FText::FromString(TEXT("Your home - close")) : FText::FromString(TEXT("Your home - open")); }
	return bOpen ? FText::FromString(TEXT("Close door")) : FText::FromString(TEXT("Open door"));
}

void ACityDoor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	// Open als: handmatig geopend, OF een NPC staat ervoor (bewoner komt naar buiten), OF (bij een
	// niet-vergrendelde deur) een speler staat ervoor. Een vergrendelde deur opent niet voor de speler.
	const bool bEffectiveOpen = bOpen || (NpcNear > 0) || (!bLocked && OtherNear > 0);
	const float Target = bEffectiveOpen ? -95.f : 0.f;
	CurAngle = FMath::FInterpTo(CurAngle, Target, DeltaSeconds, 7.f);
	if (Hinge) { Hinge->SetRelativeRotation(FRotator(0.f, CurAngle, 0.f)); }

	// Blokkeer de speler ALLEEN als de deur helemaal dicht is; zodra 'ie open(t) negeert het paneel
	// de pawn zodat het zwaaiende paneel je niet wegduwt en je er vrij door kunt lopen.
	if (Panel)
	{
		const bool bBlockPawn = (!bEffectiveOpen && FMath::Abs(CurAngle) < 2.f);
		Panel->SetCollisionResponseToChannel(ECC_Pawn, bBlockPawn ? ECR_Block : ECR_Ignore);
	}
}
