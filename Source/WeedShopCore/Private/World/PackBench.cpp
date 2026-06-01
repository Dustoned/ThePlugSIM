#include "World/PackBench.h"

#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "Placement/PlaceableTypes.h"
#include "Engine/StaticMesh.h"
#include "Net/UnrealNetwork.h"

APackBench::APackBench()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	SetRootComponent(Mesh);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> MatFinder(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (CubeFinder.Succeeded()) { Mesh->SetStaticMesh(CubeFinder.Object); }
	// Werktafel: breed, laag (~130 x 70 x 90 cm). Exacte schaal komt uit de tier-def.
	Mesh->SetWorldScale3D(FVector(1.3f, 0.7f, 0.9f));
	Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	if (MatFinder.Succeeded())
	{
		if (UMaterialInstanceDynamic* MID = Mesh->CreateDynamicMaterialInstance(0, MatFinder.Object))
		{
			MID->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.30f, 0.40f, 0.55f)); // staalblauw
		}
	}
}

void APackBench::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(APackBench, BenchTier);
}

void APackBench::SetupVisual()
{
	FPlaceableDef Def;
	if (Mesh && GetPlaceableDef(BenchTier, Def))
	{
		Mesh->SetWorldScale3D(Def.MeshScale);
	}
}

void APackBench::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	SetupVisual();
}

void APackBench::OnRep_Tier()
{
	SetupVisual();
}

void APackBench::BeginPlay()
{
	Super::BeginPlay();
	SetupVisual();
}

int32 APackBench::PackPerActionFor(FName Tier)
{
	const FString S = Tier.ToString();
	if (S == TEXT("Bench_Pack2")) { return 3; }
	if (S == TEXT("Bench_Pack3")) { return 6; }
	return 1; // Bench_Pack (basis)
}

int32 APackBench::GetPackPerAction() const { return PackPerActionFor(BenchTier); }

void APackBench::Interact_Implementation(APawn* InstigatorPawn)
{
	// Het verpak-menu openen gebeurt lokaal in de character (UI-actie); hier niets te doen.
}

FText APackBench::GetInteractionPrompt_Implementation() const
{
	return FText::FromString(FString::Printf(TEXT("Use packing bench - bags %d at a time"), GetPackPerAction()));
}
