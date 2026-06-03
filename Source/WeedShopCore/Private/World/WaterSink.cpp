#include "World/WaterSink.h"
#include "UI/WeedToast.h"

#include "Cultivation/WaterCanComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Engine.h"
#include "GameFramework/Pawn.h"
#include "UObject/ConstructorHelpers.h"
#include "Placement/PropMeshKit.h"

AWaterSink::AWaterSink()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	// Verborgen collision-doos, gecentreerd op de root (plaatsing zet de root op vloer + halve hoogte).
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Root);
	Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Mesh->SetMobility(EComponentMobility::Movable);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeFinder.Succeeded())
	{
		Mesh->SetStaticMesh(CubeFinder.Object);
		Mesh->SetRelativeScale3D(FVector(0.8f, 0.55f, 0.9f));
		Mesh->SetRelativeLocation(FVector::ZeroVector);
	}
	Mesh->SetVisibility(false);

	// Samengesteld gootsteen-kastje. Root zit in het MIDDEN (vloer = -45). Maten in cm.
	Deco = PropKit::MakeDeco(this, Root, TEXT("Deco"));
	const float Floor = -45.f;
	const FLinearColor Cabinet(0.82f, 0.80f, 0.76f); // licht kastje
	const FLinearColor Counter(0.55f, 0.57f, 0.60f); // grijs werkblad
	const FLinearColor Steel(0.70f, 0.72f, 0.75f);   // rvs
	const FLinearColor Basin(0.30f, 0.32f, 0.35f);   // donkere bak
	const FLinearColor Seam(0.45f, 0.44f, 0.42f);

	PropKit::AddPart(this, Deco, TEXT("Cabinet"),  PropKit::Cube(),     FVector(76.f, 52.f, 62.f), FVector(0.f, 0.f, Floor + 31.f), Cabinet);
	PropKit::AddPart(this, Deco, TEXT("DoorSeam"), PropKit::Cube(),     FVector(2.f, 53.f, 56.f),  FVector(0.f, 0.f, Floor + 30.f), Seam);
	PropKit::AddPart(this, Deco, TEXT("Counter"),  PropKit::Cube(),     FVector(82.f, 56.f, 6.f),  FVector(0.f, 0.f, Floor + 65.f), Counter);
	PropKit::AddPart(this, Deco, TEXT("BasinRim"), PropKit::Cube(),     FVector(46.f, 40.f, 7.f),  FVector(0.f, 0.f, Floor + 64.f), Steel);
	PropKit::AddPart(this, Deco, TEXT("BasinHole"),PropKit::Cube(),     FVector(36.f, 30.f, 6.f),  FVector(0.f, 0.f, Floor + 63.f), Basin);
	PropKit::AddPart(this, Deco, TEXT("FaucetBase"),PropKit::Cube(),    FVector(8.f, 8.f, 4.f),    FVector(0.f, -16.f, Floor + 70.f), Steel);
	PropKit::AddPart(this, Deco, TEXT("FaucetNeck"),PropKit::Cylinder(),FVector(5.f, 5.f, 22.f),   FVector(0.f, -16.f, Floor + 81.f), Steel);
	PropKit::AddPart(this, Deco, TEXT("FaucetSpout"),PropKit::Cylinder(),FVector(4.5f, 4.5f, 16.f),FVector(0.f, -9.f, Floor + 90.f), Steel, FRotator(0.f, 0.f, 90.f));
}

void AWaterSink::Interact_Implementation(APawn* InstigatorPawn)
{
	if (!HasAuthority())
	{
		return;
	}
	UWaterCanComponent* Can = InstigatorPawn ? InstigatorPawn->FindComponentByClass<UWaterCanComponent>() : nullptr;
	if (!Can || !Can->HasBottle())
	{
		if (GEngine)
		{
			UWeedToast::Notify(-1, 2.5f, FColor::Orange, TEXT("You need a water bottle (buy one from the supplier)."));
		}
		return;
	}
	Can->Fill();
	if (GEngine)
	{
		UWeedToast::Notify(-1, 2.5f, FColor::Cyan,
			FString::Printf(TEXT("Bottle filled (%d/%d)."), Can->GetCharges(), Can->GetMaxCharges()));
	}
}

FText AWaterSink::GetInteractionPrompt_Implementation() const
{
	return NSLOCTEXT("WeedShop", "FillBottle", "Fill water bottle");
}
