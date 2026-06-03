#include "World/WaterSink.h"
#include "UI/WeedToast.h"

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
	// Movable: deze actor wordt at-runtime geplaatst/opgepakt. Static zou de render-proxy op de
	// wereld-origin laten staan terwijl de actor elders spawnt (de "in het midden + dubbel"-bug).
	Mesh->SetMobility(EComponentMobility::Movable);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeFinder.Succeeded())
	{
		Mesh->SetStaticMesh(CubeFinder.Object);
		// Mesh GECENTREERD op de root (relative 0): de plaatsing zet de root op vloer + halve hoogte,
		// net als bij alle andere placeables, dus de gootsteen staat netjes met z'n onderkant op de vloer.
		// Schaal = de ghost-schaal (def MeshScale) zodat het geplaatste model exact de preview matcht.
		Mesh->SetRelativeScale3D(FVector(0.8f, 0.55f, 0.9f));
		Mesh->SetRelativeLocation(FVector::ZeroVector);
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
