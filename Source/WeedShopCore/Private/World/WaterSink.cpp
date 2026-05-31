#include "World/WaterSink.h"

#include "Cultivation/WaterCanComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Engine.h"
#include "GameFramework/Pawn.h"
#include "UObject/ConstructorHelpers.h"

AWaterSink::AWaterSink()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Root);
	Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Mesh->SetMobility(EComponentMobility::Static);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeFinder.Succeeded())
	{
		Mesh->SetStaticMesh(CubeFinder.Object);
		// Gootsteen-kastje: ~60 breed, 45 diep, 85 hoog; onderkant op de vloer.
		Mesh->SetRelativeScale3D(FVector(0.6f, 0.45f, 0.85f));
		Mesh->SetRelativeLocation(FVector(0.f, 0.f, 42.5f));
	}
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
			GEngine->AddOnScreenDebugMessage(-1, 2.5f, FColor::Orange, TEXT("You need a water bottle (buy one from the supplier)."));
		}
		return;
	}
	Can->Fill();
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 2.5f, FColor::Cyan,
			FString::Printf(TEXT("Bottle filled (%d/%d)."), Can->GetCharges(), Can->GetMaxCharges()));
	}
}

FText AWaterSink::GetInteractionPrompt_Implementation() const
{
	return NSLOCTEXT("WeedShop", "FillBottle", "Fill water bottle");
}
