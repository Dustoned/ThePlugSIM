#include "World/StorageShelf.h"

#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"
#include "Placement/PlaceableTypes.h"
#include "Engine/StaticMesh.h"
#include "Engine/Engine.h"
#include "Net/UnrealNetwork.h"

AStorageShelf::AStorageShelf()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	SetRootComponent(Mesh);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> MatFinder(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (CubeFinder.Succeeded()) { Mesh->SetStaticMesh(CubeFinder.Object); }
	Mesh->SetWorldScale3D(FVector(1.5f, 0.4f, 1.7f)); // exacte schaal komt uit de tier-def
	Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	if (MatFinder.Succeeded()) { Mesh->SetMaterial(0, MatFinder.Object); }
}

void AStorageShelf::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AStorageShelf, ShelfTier);
	DOREPLIFETIME(AStorageShelf, Contents);
}

void AStorageShelf::SetupVisual()
{
	FPlaceableDef Def;
	if (Mesh && GetPlaceableDef(ShelfTier, Def))
	{
		Mesh->SetWorldScale3D(Def.MeshScale);
	}
}

void AStorageShelf::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	SetupVisual();
}

void AStorageShelf::OnRep_Tier()
{
	SetupVisual();
}

void AStorageShelf::BeginPlay()
{
	Super::BeginPlay();
	SetupVisual();
}

int32 AStorageShelf::ServerStore(FName ItemId, int32 Count, float Thc, float QualityPct)
{
	if (!HasAuthority() || ItemId.IsNone() || Count <= 0) { return 0; }

	// Zelfde item + THC + kwaliteit -> samenvoegen op een bestaande stapel.
	for (FShelfStack& S : Contents)
	{
		if (S.ItemId == ItemId && FMath::IsNearlyEqual(S.Thc, Thc, 0.5f) && FMath::IsNearlyEqual(S.QualityPct, QualityPct, 0.5f))
		{
			S.Quantity += Count;
			return Count;
		}
	}
	// Anders een nieuwe stapel (als er ruimte is).
	if (Contents.Num() >= Capacity) { return 0; }
	FShelfStack NewS;
	NewS.ItemId = ItemId; NewS.Quantity = Count; NewS.Thc = Thc; NewS.QualityPct = QualityPct;
	Contents.Add(NewS);
	return Count;
}

int32 AStorageShelf::ServerTake(int32 SlotIndex, int32 Count, FName& OutId, float& OutThc, float& OutQualityPct)
{
	if (!HasAuthority() || !Contents.IsValidIndex(SlotIndex) || Count <= 0) { return 0; }
	FShelfStack& S = Contents[SlotIndex];
	const int32 Taken = FMath::Min(Count, S.Quantity);
	OutId = S.ItemId; OutThc = S.Thc; OutQualityPct = S.QualityPct;
	S.Quantity -= Taken;
	if (S.Quantity <= 0) { Contents.RemoveAt(SlotIndex); }
	return Taken;
}

void AStorageShelf::Interact_Implementation(APawn* InstigatorPawn)
{
	// Het schap-menu openen gebeurt lokaal in de character (UI-actie); hier niets te doen.
}

FText AStorageShelf::GetInteractionPrompt_Implementation() const
{
	return FText::FromString(FString::Printf(TEXT("Storage shelf  (%d/%d slots)"), Contents.Num(), Capacity));
}
