#include "Placement/BuildComponent.h"

#include "WeedShopCore.h"
#include "Cultivation/GrowPlant.h"
#include "Inventory/InventoryComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Controller.h"
#include "UObject/ConstructorHelpers.h"

UBuildComponent::UBuildComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	SetIsReplicatedByDefault(true); // Server-RPC routing
}

void UBuildComponent::BeginPlay()
{
	Super::BeginPlay();
}

UInventoryComponent* UBuildComponent::GetOwnerInventory() const
{
	return GetOwner() ? GetOwner()->FindComponentByClass<UInventoryComponent>() : nullptr;
}

bool UBuildComponent::GetViewPoint(FVector& OutLocation, FRotator& OutRotation) const
{
	const APawn* OwnerPawn = Cast<APawn>(GetOwner());
	if (!OwnerPawn)
	{
		return false;
	}
	if (const AController* C = OwnerPawn->GetController())
	{
		C->GetPlayerViewPoint(OutLocation, OutRotation);
		return true;
	}
	OwnerPawn->GetActorEyesViewPoint(OutLocation, OutRotation);
	return true;
}

void UBuildComponent::TogglePotPlacement()
{
	if (bPlacing)
	{
		CancelPlacing();
		return;
	}
	StartPlacing(FName(TEXT("Pot")));
}

void UBuildComponent::StartPlacing(FName ItemId)
{
	UInventoryComponent* Inv = GetOwnerInventory();
	if (!Inv || !Inv->HasItem(ItemId, 1))
	{
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 2.5f, FColor::Orange,
				TEXT("No pot to place — buy one from the supplier (phone)."));
		}
		return;
	}

	PlacingItemId = ItemId;
	bPlacing = true;
	bValidSpot = false;

	// Maak de lokale spook-mesh aan (lichtgroen, geen collision) als die er nog niet is.
	if (!Ghost)
	{
		Ghost = NewObject<UStaticMeshComponent>(GetOwner());
		if (Ghost)
		{
			Ghost->SetupAttachment(GetOwner()->GetRootComponent());
			Ghost->RegisterComponent();
			Ghost->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Ghost->SetCastShadow(false);
			Ghost->SetAbsolute(true, true, true); // niet meebewegen met de pawn; we zetten de wereld-transform zelf
			static ConstructorHelpers::FObjectFinder<UStaticMesh> CylFinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
			if (CylFinder.Succeeded())
			{
				Ghost->SetStaticMesh(CylFinder.Object);
			}
		}
	}
	if (Ghost)
	{
		Ghost->SetWorldScale3D(FVector(0.5f, 0.5f, 0.4f));
		Ghost->SetVisibility(true);
	}
}

void UBuildComponent::CancelPlacing()
{
	bPlacing = false;
	bValidSpot = false;
	if (Ghost)
	{
		Ghost->SetVisibility(false);
	}
}

void UBuildComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	const APawn* OwnerPawn = Cast<APawn>(GetOwner());
	if (!bPlacing || !OwnerPawn || !OwnerPawn->IsLocallyControlled())
	{
		return;
	}

	FVector ViewLoc; FRotator ViewRot;
	if (!GetViewPoint(ViewLoc, ViewRot))
	{
		return;
	}

	const FVector Start = ViewLoc;
	const FVector End = ViewLoc + ViewRot.Vector() * PlaceDistance;

	FCollisionQueryParams Params(SCENE_QUERY_STAT(WeedShopPlaceTrace), false);
	Params.AddIgnoredActor(GetOwner());

	FHitResult Hit;
	bValidSpot = GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params);

	if (bValidSpot)
	{
		PreviewLocation = Hit.ImpactPoint;
		// Recht overeind, alleen de yaw van de speler meenemen.
		PreviewRotation = FRotator(0.f, ViewRot.Yaw, 0.f);
	}

	if (Ghost)
	{
		Ghost->SetVisibility(bValidSpot);
		if (bValidSpot)
		{
			Ghost->SetWorldLocationAndRotation(PreviewLocation + FVector(0.f, 0.f, 20.f), PreviewRotation);
		}
	}
}

void UBuildComponent::ConfirmPlacement()
{
	if (!bPlacing || !bValidSpot)
	{
		return;
	}
	ServerPlace(PlacingItemId, PreviewLocation, PreviewRotation);
	CancelPlacing();
}

void UBuildComponent::ServerPlace_Implementation(FName ItemId, FVector Location, FRotator Rotation)
{
	UInventoryComponent* Inv = GetOwnerInventory();
	if (!Inv || !Inv->RemoveItem(ItemId, 1))
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = GetOwner();
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	// Nu alleen de pot (AGrowPlant). Later: item-id -> placeable-class via een tabel.
	AGrowPlant* Pot = World->SpawnActor<AGrowPlant>(AGrowPlant::StaticClass(), Location, Rotation, SpawnParams);
	if (Pot && GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 2.5f, FColor::Green, TEXT("Pot placed — look at it and press interact to plant a seed."));
	}
}
