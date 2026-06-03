#include "World/Atm.h"

#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "Placement/PropMeshKit.h"
#include "Engine/StaticMesh.h"

AAtm::AAtm()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	// Verborgen collision-kast: draagt footprint/collision. ~50 x 35 x 140 cm, onderkant op de vloer.
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	Mesh->SetupAttachment(Root);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeFinder.Succeeded()) { Mesh->SetStaticMesh(CubeFinder.Object); }
	Mesh->SetWorldScale3D(FVector(0.5f, 0.35f, 1.4f));
	Mesh->SetRelativeLocation(FVector(0.f, 0.f, 70.f)); // midden op halve hoogte -> onderkant op vloer
	Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Mesh->SetVisibility(false);

	// Samengestelde geldautomaat (front = +X). Maten in cm, vloer = z=0.
	Deco = PropKit::MakeDeco(this, Root, TEXT("Deco"));
	const FLinearColor BodyC(0.17f, 0.19f, 0.23f);    // donker staal
	const FLinearColor Trim(0.09f, 0.10f, 0.12f);
	const FLinearColor ScreenC(0.20f, 0.55f, 0.95f);  // blauw scherm
	const FLinearColor Money(0.18f, 0.62f, 0.34f);    // groene logo-strip
	const FLinearColor Key(0.30f, 0.31f, 0.34f);
	const float FX = 24.f; // voorvlak op +X (halve diepte)

	PropKit::AddPart(this, Deco, TEXT("Cabinet"), PropKit::Cube(), FVector(48.f, 33.f, 132.f), FVector(0.f, 0.f, 70.f),  BodyC);
	PropKit::AddPart(this, Deco, TEXT("Plinth"),  PropKit::Cube(), FVector(50.f, 35.f, 8.f),   FVector(0.f, 0.f, 4.f),   Trim);
	PropKit::AddPart(this, Deco, TEXT("Top"),     PropKit::Cube(), FVector(50.f, 35.f, 10.f),  FVector(0.f, 0.f, 137.f), Trim);
	PropKit::AddPart(this, Deco, TEXT("Logo"),    PropKit::Cube(), FVector(4.f, 30.f, 9.f),    FVector(FX - 1.f, 0.f, 120.f), Money);
	PropKit::AddPart(this, Deco, TEXT("Screen"),  PropKit::Cube(), FVector(4.f, 26.f, 22.f),   FVector(FX - 1.f, 0.f, 98.f),  ScreenC);
	PropKit::AddPart(this, Deco, TEXT("Keypad"),  PropKit::Cube(), FVector(5.f, 20.f, 12.f),   FVector(FX - 1.f, 0.f, 80.f),  Key);
	PropKit::AddPart(this, Deco, TEXT("CashSlot"),PropKit::Cube(), FVector(4.f, 22.f, 3.f),    FVector(FX - 1.f, 0.f, 64.f),  Trim);
}

void AAtm::Interact_Implementation(APawn* InstigatorPawn)
{
	// Het openen van de Bank-app gebeurt lokaal in de character (UI-actie); hier niets te doen.
}

FText AAtm::GetInteractionPrompt_Implementation() const
{
	return NSLOCTEXT("WeedShop", "AtmPrompt", "Use ATM - deposit cash (bank)");
}
