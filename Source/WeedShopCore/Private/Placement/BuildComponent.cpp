#include "Placement/BuildComponent.h"

#include "WeedShopCore.h"
#include "Cultivation/GrowPlant.h"
#include "Placement/PlaceableTypes.h"
#include "Placement/PlaceableProp.h"
#include "Inventory/InventoryComponent.h"
#include "Phone/PhoneClientComponent.h"
#include "Interaction/InteractionComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/World.h"
#include "Engine/OverlapResult.h"
#include "EngineUtils.h"
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
	DOREPLIFETIME(UBuildComponent, RepItemId);
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
	FPlaceableDef Def;
	if (!Inv || !Inv->HasItem(ItemId, 1) || !GetPlaceableDef(ItemId, Def))
	{
		return;
	}

	PlacingItemId = ItemId;
	CurrentDef = Def;
	bPlacing = true;
	bValidSpot = false;

	EnsureGhost();
	if (Ghost)
	{
		// Ghost-mesh/-schaal van het gekozen item.
		if (UStaticMesh* M = LoadObject<UStaticMesh>(nullptr, Def.MeshPath))
		{
			Ghost->SetStaticMesh(M);
		}
		Ghost->SetWorldScale3D(Def.MeshScale);
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

	// Oppakken: houd G ingedrukt terwijl je een pot aankijkt -> na PickupHoldDuration terug
	// als item in de inventory (server-authoritative).
	{
		AActor* Focus = nullptr;
		if (const UInteractionComponent* IC = GetOwner()->FindComponentByClass<UInteractionComponent>())
		{
			Focus = IC->GetFocusedActor();
		}
		const APlayerController* PC = Cast<APlayerController>(OwnerPawn->GetController());
		AGrowPlant* FocusPot = Cast<AGrowPlant>(Focus);
		if (PC && FocusPot && PC->IsInputKeyDown(EKeys::G))
		{
			PickupHoldAccum += DeltaTime;
			if (PickupHoldAccum >= PickupHoldDuration)
			{
				ServerPickup(FocusPot);
				PickupHoldAccum = 0.f;
			}
		}
		else
		{
			PickupHoldAccum = 0.f;
		}
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
		FPlaceableDef HeldDef;
		const bool bPlaceable = GetPlaceableDef(Held, HeldDef);

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
				// Mik je (onder welke hoek dan ook) op een pot -> nooit geldig (geen stapelen).
				bool bOnPlaceable = (Cast<AGrowPlant>(Hit.GetActor()) != nullptr) || (Cast<APlaceableProp>(Hit.GetActor()) != nullptr);

				// Shift ingedrukt -> snap XY op het raster (en yaw op 90°-stappen) voor nette rijen.
				const APlayerController* PC = Cast<APlayerController>(OwnerPawn->GetController());
				const bool bSnap = PC && (PC->IsInputKeyDown(EKeys::LeftShift) || PC->IsInputKeyDown(EKeys::RightShift));
				if (bSnap && GridSize > 1.f)
				{
					// Veranker het raster aan de dichtstbijzijnde bestaande pot, zodat een rij
					// netjes doorloopt vanaf de pot die je (vrij) tegen de muur zette. Geen pot
					// in de buurt -> gewoon het wereld-raster.
					float AnchorX = 0.f, AnchorY = 0.f, BestSq = TNumericLimits<float>::Max();
					for (TActorIterator<AActor> It(GetWorld()); It; ++It)
					{
						AActor* A = *It; if (!Cast<AGrowPlant>(A) && !Cast<APlaceableProp>(A)) { continue; } const FVector L = A->GetActorLocation();
						const float dSq = FMath::Square(L.X - PreviewLocation.X) + FMath::Square(L.Y - PreviewLocation.Y);
						if (dSq < BestSq)
						{
							BestSq = dSq; AnchorX = L.X; AnchorY = L.Y;
						}
					}
					PreviewLocation.X = AnchorX + FMath::GridSnap<float>(PreviewLocation.X - AnchorX, GridSize);
					PreviewLocation.Y = AnchorY + FMath::GridSnap<float>(PreviewLocation.Y - AnchorY, GridSize);
					PreviewRotation.Yaw = FMath::GridSnap<float>(PreviewRotation.Yaw, 90.f);

					// Vloer-hoogte op de gesnapte XY opnieuw bepalen (recht naar beneden tracen).
					FHitResult DownHit;
					const FVector DStart(PreviewLocation.X, PreviewLocation.Y, PreviewLocation.Z + 250.f);
					const FVector DEnd(PreviewLocation.X, PreviewLocation.Y, PreviewLocation.Z - 250.f);
					if (GetWorld()->LineTraceSingleByChannel(DownHit, DStart, DEnd, ECC_Visibility, Params))
					{
						PreviewLocation.Z = DownHit.ImpactPoint.Z;
						FloorNormalZ = DownHit.ImpactNormal.Z;
						bOnPlaceable = (Cast<AGrowPlant>(DownHit.GetActor()) != nullptr) || (Cast<APlaceableProp>(DownHit.GetActor()) != nullptr);
					}
				}

				// Alleen op grondniveau plaatsen: niet bovenop een pot/tafel/ander object
				// (dat ligt hoger dan je voeten). Plus vlakke vloer, niet op een pot, genoeg ruimte.
				const bool bFloor = FloorNormalZ > 0.7f;
				const float FeetZ = OwnerPawn->GetActorLocation().Z - OwnerPawn->GetSimpleCollisionHalfHeight();
				const bool bGroundLevel = FMath::Abs(PreviewLocation.Z - FeetZ) < 30.f;
				bValidSpot = bFloor && bGroundLevel && !bOnPlaceable
						&& !IsSpotBlocked(PreviewLocation, CurrentDef.BoxHalf, PreviewRotation.Yaw, CurrentDef.bIsPot);
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
			Ghost->SetWorldLocationAndRotation(PreviewLocation + FVector(0.f, 0.f, CurrentDef.BoxHalf.Z), PreviewRotation);
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
		ServerUpdatePreview(bShow, PreviewLocation, PreviewRotation.Yaw, bValidSpot, PlacingItemId);
	}
}

void UBuildComponent::ServerUpdatePreview_Implementation(bool bInPlacing, FVector Location, float Yaw, bool bValid, FName InItemId)
{
	bRepPlacing = bInPlacing;
	RepLocation = Location;
	RepYaw = Yaw;
	bRepValid = bValid;
	RepItemId = InItemId;
}

void UBuildComponent::ServerPickup_Implementation(AActor* Target)
{
	if (!Target)
	{
		return;
	}
	// Afstand-check (anti-cheat/lag): object moet dicht bij de speler staan.
	if (GetOwner() && FVector::Dist(GetOwner()->GetActorLocation(), Target->GetActorLocation()) > PlaceDistance + 150.f)
	{
		return;
	}

	FName ReturnItem = NAME_None;

	if (AGrowPlant* Pot = Cast<AGrowPlant>(Target))
	{
		// Pot met plant erin -> eerst oogsten.
		if (Pot->IsPlanted())
		{
			if (GEngine)
			{
				GEngine->AddOnScreenDebugMessage(-1, 2.5f, FColor::Orange, TEXT("Harvest the plant before picking up the pot."));
			}
			return;
		}
		ReturnItem = FName(TEXT("Pot"));
	}
	else if (const APlaceableProp* Prop = Cast<APlaceableProp>(Target))
	{
		ReturnItem = Prop->ItemId;
	}
	else
	{
		return; // niet oppakbaar
	}

	if (UInventoryComponent* Inv = GetOwnerInventory())
	{
		Inv->AddItem(ReturnItem, 1);
	}
	Target->Destroy();
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 2.f, FColor::Green, TEXT("Picked up."));
	}
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
		// Mesh/schaal van het item dat de andere speler plaatst.
		FPlaceableDef Def;
		float HalfZ = 20.f;
		if (GetPlaceableDef(RepItemId, Def))
		{
			if (UStaticMesh* M = LoadObject<UStaticMesh>(nullptr, Def.MeshPath))
			{
				Ghost->SetStaticMesh(M);
			}
			Ghost->SetWorldScale3D(Def.MeshScale);
			HalfZ = Def.BoxHalf.Z;
		}
		Ghost->SetWorldLocationAndRotation(RepLocation + FVector(0.f, 0.f, HalfZ), FRotator(0.f, RepYaw, 0.f));
		if (GhostMID)
		{
			GhostMID->SetVectorParameterValue(TEXT("GhostColor"),
				bRepValid ? FLinearColor(0.15f, 0.5f, 1.f, 1.f) : FLinearColor(1.f, 0.15f, 0.15f, 1.f));
		}
	}
}

bool UBuildComponent::IsSpotBlocked(const FVector& FloorPoint, const FVector& BoxHalf, float Yaw, bool bPotSpacing) const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return true;
	}
	// Box op de footprint van het object, net boven de vloer (zodat de vloer niet meetelt),
	// geroteerd met de plaatsings-yaw.
	const float HalfZ = FMath::Max(4.f, BoxHalf.Z - 4.f);
	const FVector Center = FloorPoint + FVector(0.f, 0.f, BoxHalf.Z);
	const FQuat Rot = FRotator(0.f, Yaw, 0.f).Quaternion();

	FCollisionObjectQueryParams ObjParams;
	ObjParams.AddObjectTypesToQuery(ECC_WorldStatic);
	ObjParams.AddObjectTypesToQuery(ECC_WorldDynamic);

	FCollisionQueryParams QP(SCENE_QUERY_STAT(WeedShopPlaceOverlap), false);
	QP.AddIgnoredActor(GetOwner());

	// 1) Niet in muren/objecten clippen: footprint iets ingekort zodat je er strak tegenaan mag.
	const FCollisionShape ClipBox = FCollisionShape::MakeBox(FVector(FMath::Max(2.f, BoxHalf.X - 2.f), FMath::Max(2.f, BoxHalf.Y - 2.f), HalfZ));
	if (World->OverlapAnyTestByObjectType(Center, Rot, ObjParams, ClipBox, QP))
	{
		return true;
	}

	// 2) Potten hebben extra tussenruimte nodig (plant-groei): ruimere box, alleen andere potten tellen.
	if (bPotSpacing)
	{
		const FCollisionShape SpacingBox = FCollisionShape::MakeBox(FVector(BoxHalf.X + 8.f, BoxHalf.Y + 8.f, HalfZ));
		TArray<FOverlapResult> Overlaps;
		World->OverlapMultiByObjectType(Overlaps, Center, Rot, ObjParams, SpacingBox, QP);
		for (const FOverlapResult& R : Overlaps)
		{
			if (Cast<AGrowPlant>(R.GetActor()))
			{
				return true;
			}
		}
	}

	return false;
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

	FPlaceableDef Def;
	if (!GetPlaceableDef(ItemId, Def))
	{
		return;
	}

	// Server-side her-validatie (anti-cheat / lag): niet in een muur of (voor potten) te dicht.
	if (IsSpotBlocked(Location, Def.BoxHalf, Rotation.Yaw, Def.bIsPot))
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

	if (Def.bIsPot)
	{
		World->SpawnActor<AGrowPlant>(AGrowPlant::StaticClass(), Location, Rotation, SpawnParams);
	}
	else
	{
		// Generieke placeable: ItemId vóór constructie zetten zodat de mesh meteen klopt.
		APlaceableProp* Prop = World->SpawnActorDeferred<APlaceableProp>(
			APlaceableProp::StaticClass(), FTransform(Rotation, Location),
			GetOwner(), nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (Prop)
		{
			Prop->ItemId = ItemId;
			Prop->FinishSpawning(FTransform(Rotation, Location));
		}
	}
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 2.f, FColor::Green, TEXT("Placed."));
	}
}
