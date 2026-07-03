#include "World/StoreCounter.h"

#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/StaticMesh.h"

// Statische registry van alle levende balies (zie GetAll in de header): gevuld in BeginPlay,
// geleegd in EndPlay. Hot paths (afspraak-meet-spots) lopen hierdoor O(instanties).
static TArray<TWeakObjectPtr<AStoreCounter>> GStoreCounterRegistry;
const TArray<TWeakObjectPtr<AStoreCounter>>& AStoreCounter::GetAll() { return GStoreCounterRegistry; }

void AStoreCounter::BeginPlay()
{
	Super::BeginPlay();
	GStoreCounterRegistry.Add(this);
}

void AStoreCounter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GStoreCounterRegistry.Remove(this);
	Super::EndPlay(EndPlayReason);
}

AStoreCounter::AStoreCounter()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false; // lokaal gespawnd door de (lokale) DoorRetrofitter; interactie opent lokale UI

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));

	// Balie-blok (heeft collision zodat de interactie-trace 'm raakt).
	Desk = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Desk"));
	Desk->SetupAttachment(Root);
	if (Cube) { Desk->SetStaticMesh(Cube); }
	Desk->SetRelativeScale3D(FVector(1.6f, 0.6f, 1.05f)); // ~160 x 60 x 105 cm
	Desk->SetRelativeLocation(FVector(0.f, 0.f, 52.f));
	Desk->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);

	// Gekleurd paneel als accent / "kassa".
	Panel = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Panel"));
	Panel->SetupAttachment(Root);
	if (Cube) { Panel->SetStaticMesh(Cube); }
	Panel->SetRelativeScale3D(FVector(1.62f, 0.2f, 0.3f));
	Panel->SetRelativeLocation(FVector(0.f, 0.f, 90.f));
	Panel->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

FLinearColor AStoreCounter::KindColor(EShopKind Kind)
{
	// Winkel-soort-kleuren (voorheen dubbel inline in DoorRetrofitter + PhoneClientComponent):
	// Grow = groen, Supplies = blauw, Furniture = paars. GasStation = blauw (zelfde supply-cat),
	// Apartment = warm goud (geen winkel, maar wel een herkenbaar accent).
	switch (Kind)
	{
	case EShopKind::Grow:       return FLinearColor(0.30f, 0.85f, 0.35f);
	case EShopKind::Supplies:   return FLinearColor(0.30f, 0.65f, 0.95f);
	case EShopKind::Furniture:  return FLinearColor(0.65f, 0.45f, 0.85f);
	case EShopKind::GasStation: return FLinearColor(0.30f, 0.65f, 0.95f);
	case EShopKind::Apartment:  return FLinearColor(1.00f, 0.82f, 0.25f);
	default:                    return FLinearColor(0.30f, 0.65f, 0.95f);
	}
}

void AStoreCounter::SetupVisual(const FLinearColor& Accent)
{
	if (UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial")))
	{
		if (Desk) { if (UMaterialInstanceDynamic* M = Desk->CreateDynamicMaterialInstance(0, Base)) { M->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.18f, 0.18f, 0.20f)); } }
		if (Panel) { if (UMaterialInstanceDynamic* M = Panel->CreateDynamicMaterialInstance(0, Base)) { M->SetVectorParameterValue(TEXT("Color"), Accent); } }
	}
}

int32 AStoreCounter::GetShopApp() const
{
	// App-indices: 1 = Grow shop, 7 = Supplies-app (met subcategorieen).
	switch (Kind)
	{
	case EShopKind::Grow: return 1;
	case EShopKind::Furniture:
	case EShopKind::Supplies:
	case EShopKind::GasStation: return 7;
	default: return 7;
	}
}

int32 AStoreCounter::GetShopCat() const
{
	// SupplierCat: 0=Seeds,1=Pots,2=Drying,3=Packing,4=Papers,5=Soil,6=Water,7=Furniture.
	switch (Kind)
	{
	case EShopKind::Furniture:  return 7;
	case EShopKind::Supplies:   return 4; // papers/verpakking
	case EShopKind::GasStation: return 4;
	case EShopKind::Grow:       return 0; // seeds (grow-app heeft eigen tabs)
	default: return 0;
	}
}

bool AStoreCounter::HasShop() const
{
	return Kind != EShopKind::Apartment;
}

void AStoreCounter::Interact_Implementation(APawn* InstigatorPawn)
{
	// De character opent de telefoon-shop lokaal (net als ATM/verpaktafel); hier niets te doen.
}

FText AStoreCounter::GetInteractionPrompt_Implementation() const
{
	switch (Kind)
	{
	case EShopKind::Grow:       return FText::FromString(TEXT("Grow shop"));
	case EShopKind::Furniture:  return FText::FromString(TEXT("Furniture store"));
	case EShopKind::Supplies:   return FText::FromString(TEXT("Supplies store"));
	case EShopKind::GasStation: return FText::FromString(TEXT("Gas station shop"));
	case EShopKind::Apartment:  return FText::FromString(TEXT("Apartment (buying coming soon)"));
	default: return FText::FromString(TEXT("Shop"));
	}
}
