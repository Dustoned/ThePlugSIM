#include "World/PackBench.h"

#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "Placement/PlaceableTypes.h"
#include "Placement/PropMeshKit.h"
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

	// Samengestelde look: de saaie kubus wordt verborgen; we bouwen een echt werkbankje.
	Deco = PropKit::MakeDeco(this, Mesh, TEXT("Deco"));
	for (int32 i = 0; i < 8; ++i) { Parts.Add(PropKit::MakePart(this, Deco, *FString::Printf(TEXT("Part%d"), i))); }
}

void APackBench::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(APackBench, BenchTier);
}

void APackBench::SetupVisual()
{
	FPlaceableDef Def;
	if (!Mesh || !GetPlaceableDef(BenchTier, Def)) { return; }
	Mesh->SetWorldScale3D(Def.MeshScale);

	// Maten in cm; de root-kubus is gecentreerd op de actor-origin, dus de vloer ligt op -H/2.
	const float W = Def.MeshScale.X * 100.f; // breedte
	const float D = Def.MeshScale.Y * 100.f; // diepte
	const float H = Def.MeshScale.Z * 100.f; // hoogte
	const float Floor = -H * 0.5f;
	if (Parts.Num() < 8) { return; }

	const FLinearColor Wood(0.40f, 0.28f, 0.17f);
	const FLinearColor Steel(0.34f, 0.42f, 0.52f);
	const FLinearColor Top(0.62f, 0.65f, 0.70f);

	Mesh->SetVisibility(false); // saaie kubus weg; we tekenen 'm zelf

	const float TopThick = FMath::Min(8.f, H * 0.12f);
	const float LegW = FMath::Min(8.f, W * 0.08f);
	const float Inset = LegW * 0.5f + 2.f;
	// Werkblad (bovenop).
	PropKit::SetPart(Parts[0], PropKit::Cube(), FVector(W, D, TopThick), FVector(0, 0, H * 0.5f - TopThick * 0.5f), Top);
	// Onderplank.
	PropKit::SetPart(Parts[1], PropKit::Cube(), FVector(W * 0.92f, D * 0.8f, TopThick * 0.6f), FVector(0, 0, Floor + H * 0.28f), Wood);
	// 4 poten.
	const float LegH = H - TopThick;
	const float PX = W * 0.5f - Inset;
	const float PY = D * 0.5f - Inset;
	const float LegZ = Floor + LegH * 0.5f;
	PropKit::SetPart(Parts[2], PropKit::Cube(), FVector(LegW, LegW, LegH), FVector( PX,  PY, LegZ), Steel);
	PropKit::SetPart(Parts[3], PropKit::Cube(), FVector(LegW, LegW, LegH), FVector( PX, -PY, LegZ), Steel);
	PropKit::SetPart(Parts[4], PropKit::Cube(), FVector(LegW, LegW, LegH), FVector(-PX,  PY, LegZ), Steel);
	PropKit::SetPart(Parts[5], PropKit::Cube(), FVector(LegW, LegW, LegH), FVector(-PX, -PY, LegZ), Steel);
	// Een weegschaal/blokje + een rolletje zakjes als accent op het blad.
	PropKit::SetPart(Parts[6], PropKit::Cube(), FVector(W * 0.18f, D * 0.22f, 6.f), FVector(W * 0.22f, 0, H * 0.5f + 3.f), FLinearColor(0.15f, 0.16f, 0.18f));
	PropKit::SetPart(Parts[7], PropKit::Cylinder(), FVector(D * 0.18f, D * 0.18f, W * 0.16f), FVector(-W * 0.25f, 0, H * 0.5f + D * 0.09f), FLinearColor(0.85f, 0.82f, 0.6f), FRotator(0.f, 0.f, 90.f));
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

float APackBench::PackSpeedFor(FName Tier)
{
	const FString S = Tier.ToString();
	if (S == TEXT("Bench_Pack2")) { return 1.6f; } // pro: ~40% sneller per container
	if (S == TEXT("Bench_Pack3")) { return 2.4f; } // industrieel: ruim 2x zo snel
	return 1.f; // Bench_Pack (basis)
}

float APackBench::GetPackSpeed() const { return PackSpeedFor(BenchTier); }

void APackBench::Interact_Implementation(APawn* InstigatorPawn)
{
	// Het verpak-menu openen gebeurt lokaal in de character (UI-actie); hier niets te doen.
}

FText APackBench::GetInteractionPrompt_Implementation() const
{
	// Clean prompt; het "X zakjes per keer" + alle stats staan in de verpak-HUD zelf.
	return NSLOCTEXT("WeedShop", "UsePackBench", "Packing bench");
}
