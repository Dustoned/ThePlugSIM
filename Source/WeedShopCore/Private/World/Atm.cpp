#include "World/Atm.h"

#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

AAtm::AAtm()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	SetRootComponent(Mesh);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> MatFinder(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (CubeFinder.Succeeded()) { Mesh->SetStaticMesh(CubeFinder.Object); }
	// Kast: ~50 x 35 x 140 cm.
	Mesh->SetWorldScale3D(FVector(0.5f, 0.35f, 1.4f));
	Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	if (MatFinder.Succeeded()) { Mesh->SetMaterial(0, MatFinder.Object); }

	// Schermpje (blauw vlakje) bovenop, puur cosmetisch.
	Screen = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Screen"));
	Screen->SetupAttachment(Mesh);
	if (CubeFinder.Succeeded()) { Screen->SetStaticMesh(CubeFinder.Object); }
	Screen->SetRelativeLocation(FVector(0.55f, 0.f, 0.25f)); // iets naar voren/omhoog (in body-space)
	Screen->SetRelativeScale3D(FVector(0.1f, 0.8f, 0.4f));
	Screen->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	if (MatFinder.Succeeded())
	{
		UMaterialInstanceDynamic* MID = Screen->CreateDynamicMaterialInstance(0, MatFinder.Object);
		if (MID) { MID->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.2f, 0.6f, 1.f)); }
	}
}

void AAtm::Interact_Implementation(APawn* InstigatorPawn)
{
	// Het openen van de Bank-app gebeurt lokaal in de character (UI-actie); hier niets te doen.
}

FText AAtm::GetInteractionPrompt_Implementation() const
{
	return NSLOCTEXT("WeedShop", "AtmPrompt", "Use ATM - deposit cash (bank)");
}
