#include "World/PackBench.h"

#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

APackBench::APackBench()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	SetRootComponent(Mesh);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> MatFinder(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (CubeFinder.Succeeded()) { Mesh->SetStaticMesh(CubeFinder.Object); }
	// Werktafel: breed, laag (~130 x 70 x 90 cm).
	Mesh->SetWorldScale3D(FVector(1.3f, 0.7f, 0.9f));
	Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	if (MatFinder.Succeeded()) { Mesh->SetMaterial(0, MatFinder.Object); }
}

void APackBench::Interact_Implementation(APawn* InstigatorPawn)
{
	// Het verpak-menu openen gebeurt lokaal in de character (UI-actie); hier niets te doen.
}

FText APackBench::GetInteractionPrompt_Implementation() const
{
	return NSLOCTEXT("WeedShop", "PackBenchPrompt", "Use packing bench - bag your weed");
}
