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
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Net/UnrealNetwork.h"

UBuildComponent::UBuildComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	SetIsReplicatedByDefault(true); // Server-RPC routing
}

void UBuildComponent::BeginPlay()
{
	Super::BeginPlay();
}

void UBuildComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UBuildComponent, bRepPlacing);
	DOREPLIFETIME(UBuildComponent, bRepValid);
	DOREPLIFETIME(UBuildComponent, RepLocation);
	DOREPLIFETIME(UBuildComponent, RepYaw);
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

	EnsureGhost();
	if (Ghost)
	{
		Ghost->SetVisibility(true);
	}
}

void UBuildComponent::EnsureGhost()
{
	if (Ghost || !GetOwner())
	{
		return;
	}
	// Spook-mesh (geen collision); positie zetten we zelf in wereld-coördinaten.
	Ghost = NewObject<UStaticMeshComponent>(GetOwner());
	if (!Ghost)
	{
		return;
	}
	Ghost->SetupAttachment(GetOwner()->GetRootComponent());
	Ghost->RegisterComponent();
	Ghost->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Ghost->SetCastShadow(false);
	Ghost->SetAbsolute(true, true, true);
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
	Ghost->SetWorldScale3D(FVector(0.5f, 0.5f, 0.4f));
	Ghost->SetVisibility(false);
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
	if (!OwnerPawn)
	{
		return;
	}

	// Niet-lokale speler: render z'n ghost uit de gerepliceerde preview-staat (co-op zicht).
	if (!OwnerPawn->IsLocallyControlled())
	{
		UpdateRemoteGhost();
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

	bAimHit = false;
	bValidSpot = false;

	if (bPlacing)
	{
		FVector ViewLoc; FRotator ViewRot;
		if (GetViewPoint(ViewLoc, ViewRot))
		{
			const FVector Start = ViewLoc;
			const FVector End = ViewLoc + ViewRot.Vector() * PlaceDistance;

			FCollisionQueryParams Params(SCENE_QUERY_STAT(WeedShopPlaceTrace), false);
			Params.AddIgnoredActor(GetOwner());

			FHitResult Hit;
			bAimHit = GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params);
			if (bAimHit)
			{
				PreviewLocation = Hit.ImpactPoint;
				PreviewRotation = FRotator(0.f, ViewRot.Yaw, 0.f); // recht overeind, yaw van de speler
				float FloorNormalZ = Hit.ImpactNormal.Z;

				// Shift ingedrukt -> snap XY op het raster (en yaw op 90°-stappen) voor nette rijen.
				const APlayerController* PC = Cast<APlayerController>(OwnerPawn->GetController());
				const bool bSnap = PC && (PC->IsInputKeyDown(EKeys::LeftShift) || PC->IsInputKeyDown(EKeys::RightShift));
				if (bSnap && GridSize > 1.f)
				{
					PreviewLocation.X = FMath::GridSnap<float>(PreviewLocation.X, GridSize);
					PreviewLocation.Y = FMath::GridSnap<float>(PreviewLocation.Y, GridSize);
					PreviewRotation.Yaw = FMath::GridSnap<float>(PreviewRotation.Yaw, 90.f);

					// Vloer-hoogte op de gesnapte XY opnieuw bepalen (recht naar beneden tracen).
					FHitResult DownHit;
					const FVector DStart(PreviewLocation.X, PreviewLocation.Y, PreviewLocation.Z + 250.f);
					const FVector DEnd(PreviewLocation.X, PreviewLocation.Y, PreviewLocation.Z - 250.f);
					if (GetWorld()->LineTraceSingleByChannel(DownHit, DStart, DEnd, ECC_Visibility, Params))
					{
						PreviewLocation.Z = DownHit.ImpactPoint.Z;
						FloorNormalZ = DownHit.ImpactNormal.Z;
					}
				}

				// Alleen op grondniveau plaatsen: niet bovenop een pot/tafel/ander object
				// (dat ligt hoger dan je voeten). Plus vlakke vloer + genoeg vrije ruimte.
				const bool bFloor = FloorNormalZ > 0.7f;
				const float FeetZ = OwnerPawn->GetActorLocation().Z - OwnerPawn->GetSimpleCollisionHalfHeight();
				const bool bGroundLevel = FMath::Abs(PreviewLocation.Z - FeetZ) < 30.f;
				bValidSpot = bFloor && bGroundLevel && !IsSpotBlocked(PreviewLocation);
			}
		}
	}

	// Eigen ghost bijwerken (lokaal).
	const bool bShow = bPlacing && bAimHit;
	if (Ghost)
	{
		Ghost->SetVisibility(bShow);
		if (bShow)
		{
			Ghost->SetWorldLocationAndRotation(PreviewLocation + FVector(0.f, 0.f, 20.f), PreviewRotation);
		}
		if (GhostMID)
		{
			GhostMID->SetVectorParameterValue(TEXT("GhostColor"),
				bValidSpot ? FLinearColor(0.15f, 0.5f, 1.f, 1.f) : FLinearColor(1.f, 0.15f, 0.15f, 1.f));
		}
	}

	// Preview-staat naar de server sturen (getemporiseerd) zodat co-op-spelers de ghost zien.
	PreviewSendAccum += DeltaTime;
	if (PreviewSendAccum >= 0.066f)
	{
		PreviewSendAccum = 0.f;
		ServerUpdatePreview(bShow, PreviewLocation, PreviewRotation.Yaw, bValidSpot);
	}
}

void UBuildComponent::ServerUpdatePreview_Implementation(bool bInPlacing, FVector Location, float Yaw, bool bValid)
{
	bRepPlacing = bInPlacing;
	RepLocation = Location;
	RepYaw = Yaw;
	bRepValid = bValid;
}

void UBuildComponent::UpdateRemoteGhost()
{
	EnsureGhost();
	if (!Ghost)
	{
		return;
	}
	Ghost->SetVisibility(bRepPlacing);
	if (bRepPlacing)
	{
		Ghost->SetWorldLocationAndRotation(RepLocation + FVector(0.f, 0.f, 20.f), FRotator(0.f, RepYaw, 0.f));
		if (GhostMID)
		{
			GhostMID->SetVectorParameterValue(TEXT("GhostColor"),
				bRepValid ? FLinearColor(0.15f, 0.5f, 1.f, 1.f) : FLinearColor(1.f, 0.15f, 0.15f, 1.f));
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
	// Box met wat ruimte rond de pot (zodat planten genoeg plek hebben), net boven de vloer
	// zodat de vloer zelf niet meetelt maar muren/andere potten/objecten wél blokkeren.
	const FVector Center = FloorPoint + FVector(0.f, 0.f, 20.f);
	const FCollisionShape Box = FCollisionShape::MakeBox(FVector(32.f, 32.f, 16.f));

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
