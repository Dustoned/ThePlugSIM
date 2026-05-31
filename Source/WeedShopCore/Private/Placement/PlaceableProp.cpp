#include "Placement/PlaceableProp.h"

#include "Placement/PlaceableTypes.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Net/UnrealNetwork.h"

APlaceableProp::APlaceableProp()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	SetRootComponent(Mesh);
	// WorldStatic + query/physics zodat de plaats-trace en footprint-overlap 'm zien.
	Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Mesh->SetMobility(EComponentMobility::Movable);
}

void APlaceableProp::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(APlaceableProp, ItemId);
}

void APlaceableProp::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	SetupVisual();
}

void APlaceableProp::BeginPlay()
{
	Super::BeginPlay();
	SetupVisual();
}

void APlaceableProp::OnRep_ItemId()
{
	SetupVisual();
}

void APlaceableProp::SetupVisual()
{
	FPlaceableDef Def;
	if (!Mesh || !GetPlaceableDef(ItemId, Def))
	{
		return;
	}
	if (UStaticMesh* M = LoadObject<UStaticMesh>(nullptr, Def.MeshPath))
	{
		Mesh->SetStaticMesh(M);
	}
	Mesh->SetRelativeScale3D(Def.MeshScale);
	// Mesh omhoog zodat de onderkant op de actor-origin (= vloer) staat.
	Mesh->SetRelativeLocation(FVector(0.f, 0.f, Def.BoxHalf.Z));
}

FText APlaceableProp::GetInteractionPrompt_Implementation() const
{
	FPlaceableDef Def;
	const FString Name = GetPlaceableDef(ItemId, Def) ? Def.DisplayName : ItemId.ToString();
	return FText::FromString(FString::Printf(TEXT("%s — hold G to pick up"), *Name));
}
