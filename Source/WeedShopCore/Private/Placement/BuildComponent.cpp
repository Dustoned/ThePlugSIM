#include "Placement/BuildComponent.h"

#include "WeedShopCore.h"
#include "Cultivation/GrowPlant.h"
#include "Inventory/InventoryComponent.h"
#include "Phone/PhoneClientComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Controller.h"

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
			// LoadObject (geen ConstructorHelpers — die mag alleen in een constructor, anders crash).
			if (UStaticMesh* Cyl = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder")))
			{
				Ghost->SetStaticMesh(Cyl);
			}
			// Doorzichtig kleur-materiaal (blauw/rood) als dynamische instance.
			if (UMaterialInterface* GhostMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/_Project/Materials/M_PlacementGhost.M_PlacementGhost")))
			{
				GhostMID = UMaterialInstanceDynamic::Create(GhostMat, this);
				if (GhostMID)
				{
					Ghost->SetMaterial(0, GhostMID);
				}
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
	if (!OwnerPawn || !OwnerPawn->IsLocallyControlled())
	{
		return;
	}

	// Auto-preview: heb je een plaatsbaar item in de hand (en geen UI open), toon meteen de
	// preview zodat je direct kunt plaatsen. Schakel je naar iets anders, dan verdwijnt 'ie.
	{
		bool bUIOpen = false;
		if (const UPhoneClientComponent* Ph = GetOwner()->FindComponentByClass<UPhoneClientComponent>())
		{
			bUIOpen = Ph->IsOpen() || Ph->IsRollOpen() || Ph->IsDealOpen() || Ph->IsInventoryOpen();
		}
		const UInventoryComponent* Inv = GetOwnerInventory();
		const FName Held = Inv ? Inv->GetActiveItemId() : NAME_None;
		const bool bPlaceable = (Held == FName(TEXT("Pot")));

		if (bPlaceable && !bUIOpen)
		{
			if (!bPlacing || PlacingItemId != Held)
			{
				StartPlacing(Held);
			}
		}
		else if (bPlacing)
		{
			CancelPlacing();
		}
	}

	if (!bPlacing)
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
	bAimHit = GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params);

	bValidSpot = false;
	if (bAimHit)
	{
		PreviewLocation = Hit.ImpactPoint;
		// Recht overeind, alleen de yaw van de speler meenemen.
		PreviewRotation = FRotator(0.f, ViewRot.Yaw, 0.f);

		// Geldig = (vrijwel) vlakke vloer + geen overlap met muur/andere pot.
		const bool bFloor = Hit.ImpactNormal.Z > 0.7f;
		bValidSpot = bFloor && !IsSpotBlocked(PreviewLocation);
	}

	if (Ghost)
	{
		Ghost->SetVisibility(bAimHit);
		if (bAimHit)
		{
			Ghost->SetWorldLocationAndRotation(PreviewLocation + FVector(0.f, 0.f, 20.f), PreviewRotation);
		}
		if (GhostMID)
		{
			// Blauw = plaatsbaar, rood = niet (muur/andere pot/geen vloer).
			GhostMID->SetVectorParameterValue(TEXT("GhostColor"),
				bValidSpot ? FLinearColor(0.15f, 0.5f, 1.f, 1.f) : FLinearColor(1.f, 0.15f, 0.15f, 1.f));
		}
	}
}

bool UBuildComponent::IsSpotBlocked(const FVector& FloorPoint) const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return true;
	}
	// Box ter grootte van de pot, opgetild zodat de vloer zelf niet meetelt; muren en andere
	// potten binnen de footprint blokkeren wel.
	const FVector Center = FloorPoint + FVector(0.f, 0.f, 22.f);
	const FCollisionShape Box = FCollisionShape::MakeBox(FVector(24.f, 24.f, 18.f));

	FCollisionObjectQueryParams ObjParams;
	ObjParams.AddObjectTypesToQuery(ECC_WorldStatic);
	ObjParams.AddObjectTypesToQuery(ECC_WorldDynamic);

	FCollisionQueryParams QP(SCENE_QUERY_STAT(WeedShopPlaceOverlap), false);
	QP.AddIgnoredActor(GetOwner());

	return World->OverlapAnyTestByObjectType(Center, FQuat::Identity, ObjParams, Box, QP);
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
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// Server-side her-validatie (anti-cheat / lag): niet plaatsen in een muur of op een pot.
	if (IsSpotBlocked(Location))
	{
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 2.f, FColor::Red, TEXT("Can't place there (blocked)."));
		}
		return;
	}

	UInventoryComponent* Inv = GetOwnerInventory();
	if (!Inv || !Inv->RemoveItem(ItemId, 1))
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
