#include "Placement/BuildComponent.h"
#include "UI/WeedToast.h"

#include "WeedShopCore.h"
#include "Cultivation/GrowPlant.h"
#include "Placement/PlaceableTypes.h"
#include "Placement/PlaceableProp.h"
#include "World/Atm.h"
#include "World/PackBench.h"
#include "World/StorageShelf.h"
#include "World/WaterSink.h"
#include "World/CeilingLamp.h"
#include "Cultivation/DryingRack.h"
#include "Inventory/InventoryComponent.h"
#include "Phone/PhoneClientComponent.h"
#include "Interaction/InteractionComponent.h"
#include "Game/WeedShopGameState.h"
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

namespace
{
	// Testing/sandbox: overal plaatsen toegestaan (geen "alleen binnen"/grondhoogte-regel).
	bool WorldFreeBuild(const UWorld* W)
	{
		const AWeedShopGameState* GS = W ? W->GetGameState<AWeedShopGameState>() : nullptr;
		return GS && GS->IsFreeBuild();
	}
}

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
	PlaceYawOffset = 0.f;

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
	// Echt model als preview (ghost-gekleurd) -> de preview ziet er hetzelfde uit als het geplaatste object.
	SpawnPreview(Def, ItemId);
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

void UBuildComponent::DestroyPreview()
{
	if (AActor* A = PreviewActor.Get()) { A->Destroy(); }
	PreviewActor = nullptr;
}

void UBuildComponent::SpawnPreview(const FPlaceableDef& Def, FName ItemId)
{
	DestroyPreview();
	UWorld* W = GetWorld();
	if (!W) { return; }

	// Ver weg spawnen; de tick zet de echte preview-positie. Transient + AlwaysSpawn.
	const FTransform TM(FRotator::ZeroRotator, FVector(0.f, 0.f, -100000.f));
	auto Deferred = [&](UClass* Cls) -> AActor*
	{
		return W->SpawnActorDeferred<AActor>(Cls, TM, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	};

	AActor* A = nullptr;
	if (Def.bIsPot)             { if (AGrowPlant* P = Cast<AGrowPlant>(Deferred(AGrowPlant::StaticClass())))      { P->PotTier = ItemId;  P->FinishSpawning(TM); A = P; } }
	else if (Def.bIsDryRack)    { if (ADryingRack* R = Cast<ADryingRack>(Deferred(ADryingRack::StaticClass())))    { R->RackTier = ItemId;  R->FinishSpawning(TM); A = R; } }
	else if (Def.bIsPackBench)  { if (APackBench* B = Cast<APackBench>(Deferred(APackBench::StaticClass())))       { B->BenchTier = ItemId; B->FinishSpawning(TM); A = B; } }
	else if (Def.bIsShelf)      { if (AStorageShelf* S = Cast<AStorageShelf>(Deferred(AStorageShelf::StaticClass()))) { S->ShelfTier = ItemId; S->FinishSpawning(TM); A = S; } }
	else if (Def.bIsSink)       { A = W->SpawnActor<AWaterSink>(AWaterSink::StaticClass(), TM); }
	else if (Def.bIsLamp)       { A = W->SpawnActor<ACeilingLamp>(ACeilingLamp::StaticClass(), TM); }
	else if (Def.bIsAtm)        { A = W->SpawnActor<AAtm>(AAtm::StaticClass(), TM); }
	else                        { if (APlaceableProp* P = Cast<APlaceableProp>(Deferred(APlaceableProp::StaticClass()))) { P->ItemId = ItemId; P->FinishSpawning(TM); A = P; } }

	if (!A) { return; }

	// Geen botsing/schaduw; ghost-materiaal op alle (zichtbare) mesh-onderdelen -> ziet eruit als wat je plaatst.
	if (!PreviewMID)
	{
		if (UMaterialInterface* GM = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/_Project/Materials/M_PlacementGhost.M_PlacementGhost")))
		{
			PreviewMID = UMaterialInstanceDynamic::Create(GM, this);
		}
	}
	A->SetActorEnableCollision(false);
	TArray<UStaticMeshComponent*> Comps;
	A->GetComponents<UStaticMeshComponent>(Comps);
	for (UStaticMeshComponent* C : Comps)
	{
		if (!C) { continue; }
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetCastShadow(false);
		if (PreviewMID)
		{
			const int32 N = C->GetNumMaterials();
			for (int32 m = 0; m < N; ++m) { C->SetMaterial(m, PreviewMID); }
		}
	}
	PreviewActor = A;
}

void UBuildComponent::CancelPlacing()
{
	bPlacing = false;
	bValidSpot = false;
	if (Ghost)
	{
		Ghost->SetVisibility(false);
	}
	DestroyPreview();
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
		const bool bPickable = IsPickable(Focus);
		if (PC && bPickable && PC->IsInputKeyDown(EKeys::G))
		{
			PickupHoldAccum += DeltaTime;
			if (PickupHoldAccum >= PickupHoldDuration)
			{
				ServerPickup(Focus);
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
				PreviewRotation = FRotator(0.f, ViewRot.Yaw + PlaceYawOffset, 0.f); // kijkrichting + handmatige R-draai
				float FloorNormalZ = Hit.ImpactNormal.Z;
				// Mik je (onder welke hoek dan ook) op een pot -> nooit geldig (geen stapelen).
				bool bOnPlaceable = (Cast<AGrowPlant>(Hit.GetActor()) != nullptr) || (Cast<APlaceableProp>(Hit.GetActor()) != nullptr);

					if (CurrentDef.bIsWallMount)
					{
						// Wand-mount (droogrek): rug tegen een VERTICALE muur, voorkant de kamer in.
						const FVector N = Hit.ImpactNormal;
						const bool bWall = FMath::Abs(N.Z) < 0.4f; // verticaal vlak
						FVector D = FVector(N.X, N.Y, 0.f).GetSafeNormal(); // horizontale muur-normaal (de kamer in)
						if (D.IsNearlyZero()) { D = FVector(1.f, 0.f, 0.f); }
						// Lokale +Y wijst langs de normaal (de kamer in); de rug (-Y) ligt vlak tegen de muur.
						PreviewRotation = FRotator(0.f, FMath::RadiansToDegrees(FMath::Atan2(D.Y, D.X)) - 90.f, 0.f);
						// Midden = trefpunt + normaal * halve diepte (rug strak tegen de muur). Hoogte = waar je mikt.
						FVector Center = Hit.ImpactPoint + D * CurrentDef.BoxHalf.Y;
						// Shift: snap langs de muur (horizontale tangent) en in hoogte op het raster.
						const APlayerController* PCw = Cast<APlayerController>(OwnerPawn->GetController());
						if (PCw && (PCw->IsInputKeyDown(EKeys::LeftShift) || PCw->IsInputKeyDown(EKeys::RightShift)) && GridSize > 1.f)
						{
							const FVector Tang(-D.Y, D.X, 0.f); // horizontale richting langs de muur
							const float Along = FVector::DotProduct(Center, Tang);
							Center += Tang * (FMath::GridSnap<float>(Along, GridSize) - Along);
							Center.Z = FMath::GridSnap<float>(Center.Z, GridSize);
						}
						PreviewLocation = Center;
						const bool bFreeW = WorldFreeBuild(GetWorld());
						// Wand-mount (rek) hoort BINNEN (tenzij vrij-bouwen of expliciet outdoors toegestaan).
						bValidSpot = bWall && !bOnPlaceable
							&& (bFreeW || CurrentDef.bAllowOutdoors || IsIndoors(PreviewLocation));
					}
					else
					{

				// Shift ingedrukt -> snap XY op het raster (en yaw op 90°-stappen) voor nette rijen.
				const APlayerController* PC = Cast<APlayerController>(OwnerPawn->GetController());
				const bool bSnap = PC && (PC->IsInputKeyDown(EKeys::LeftShift) || PC->IsInputKeyDown(EKeys::RightShift));
				if (bSnap && GridSize > 1.f)
				{
					// Veranker het raster aan de dichtstbijzijnde bestaande pot, zodat een rij
					// netjes doorloopt vanaf de pot die je (vrij) tegen de muur zette. Geen pot
					// in de buurt -> gewoon het wereld-raster.
					float AnchorX = 0.f, AnchorY = 0.f, BestSq = FMath::Square(GridSize * 6.f); bool bHasAnchor = false;
					for (TActorIterator<AActor> It(GetWorld()); It; ++It)
					{
						AActor* A = *It; if (!Cast<AGrowPlant>(A) && !Cast<APlaceableProp>(A)) { continue; } const FVector L = A->GetActorLocation();
						const float dSq = FMath::Square(L.X - PreviewLocation.X) + FMath::Square(L.Y - PreviewLocation.Y);
						if (dSq < BestSq)
						{
							BestSq = dSq; AnchorX = L.X; AnchorY = L.Y; bHasAnchor = true;
						}
					}
					PreviewRotation.Yaw = FMath::GridSnap<float>(PreviewRotation.Yaw, 90.f);
						{
							// Effectieve footprint (X/Y) na de 90-graden draai.
							const bool bSwap = FMath::IsNearlyEqual(FMath::Fmod(FMath::Abs(PreviewRotation.Yaw), 180.f), 90.f, 45.f);
							const float EffHX = bSwap ? CurrentDef.BoxHalf.Y : CurrentDef.BoxHalf.X;
							const float EffHY = bSwap ? CurrentDef.BoxHalf.X : CurrentDef.BoxHalf.Y;
							if (bHasAnchor)
							{
								// Lijn uit op een bestaand object -> nette rij.
								PreviewLocation.X = AnchorX + FMath::GridSnap<float>(PreviewLocation.X - AnchorX, GridSize);
								PreviewLocation.Y = AnchorY + FMath::GridSnap<float>(PreviewLocation.Y - AnchorY, GridSize);
							}
							else
							{
								// Veranker het raster aan de dichtstbijzijnde muur per as (eerste cel = strak tegen de muur).
								// Lampen: trace net ONDER het plafond (anders zit je in het dak en mis je de muren).
								const FVector TS(PreviewLocation.X, PreviewLocation.Y, PreviewLocation.Z + (CurrentDef.bIsLamp ? -30.f : 30.f));
								FHitResult HX1, HX2, HY1, HY2;
								const bool bX1 = GetWorld()->LineTraceSingleByChannel(HX1, TS, TS + FVector(1500.f, 0, 0), ECC_Visibility, Params);
								const bool bX2 = GetWorld()->LineTraceSingleByChannel(HX2, TS, TS + FVector(-1500.f, 0, 0), ECC_Visibility, Params);
								float OriginX = 0.f;
								if (bX1 && (!bX2 || HX1.Distance <= HX2.Distance)) OriginX = HX1.ImpactPoint.X - EffHX;
								else if (bX2) OriginX = HX2.ImpactPoint.X + EffHX;
								PreviewLocation.X = OriginX + FMath::GridSnap<float>(PreviewLocation.X - OriginX, GridSize);

								const bool bY1 = GetWorld()->LineTraceSingleByChannel(HY1, TS, TS + FVector(0, 1500.f, 0), ECC_Visibility, Params);
								const bool bY2 = GetWorld()->LineTraceSingleByChannel(HY2, TS, TS + FVector(0, -1500.f, 0), ECC_Visibility, Params);
								float OriginY = 0.f;
								if (bY1 && (!bY2 || HY1.Distance <= HY2.Distance)) OriginY = HY1.ImpactPoint.Y - EffHY;
								else if (bY2) OriginY = HY2.ImpactPoint.Y + EffHY;
								PreviewLocation.Y = OriginY + FMath::GridSnap<float>(PreviewLocation.Y - OriginY, GridSize);
							}
						}
					if (bHasAnchor) PreviewLocation.Y = AnchorY + FMath::GridSnap<float>(PreviewLocation.Y - AnchorY, GridSize);
					PreviewRotation.Yaw = FMath::GridSnap<float>(PreviewRotation.Yaw, 90.f);

					// Hoogte op de gesnapte XY opnieuw bepalen. Plafondlampen tracen OMHOOG naar het
					// plafond (zodat de grid-snap ook aan het plafond werkt); al het andere OMLAAG naar de vloer.
					if (CurrentDef.bIsLamp)
					{
						FHitResult UpHit;
						const FVector UStart(PreviewLocation.X, PreviewLocation.Y, PreviewLocation.Z - 250.f);
						const FVector UEnd(PreviewLocation.X, PreviewLocation.Y, PreviewLocation.Z + 600.f);
						if (GetWorld()->LineTraceSingleByChannel(UpHit, UStart, UEnd, ECC_Visibility, Params))
						{
							PreviewLocation.Z = UpHit.ImpactPoint.Z;
							FloorNormalZ = UpHit.ImpactNormal.Z; // plafond -> normaal wijst omlaag
						}
					}
					else
					{
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
				}

				// Alleen op grondniveau plaatsen: niet bovenop een pot/tafel/ander object
				// (dat ligt hoger dan je voeten). Plus vlakke vloer, niet op een pot, genoeg ruimte.
				if (CurrentDef.bIsLamp)
				{
					// Plafondlamp: geldig zodra je op een PLAFOND (omlaag-wijzend vlak) mikt.
					// Een plafond raken bewijst al dat je binnen bent -> geen aparte binnen-trace.
					bValidSpot = (FloorNormalZ < -0.4f) && !bOnPlaceable;
				}
				else
				{
					const bool bFree = WorldFreeBuild(GetWorld());
					const bool bFloor = FloorNormalZ > 0.7f;
					const float FeetZ = OwnerPawn->GetActorLocation().Z - OwnerPawn->GetSimpleCollisionHalfHeight();
					const bool bGroundLevel = FMath::Abs(PreviewLocation.Z - FeetZ) < 30.f;
					// Vrij bouwen: laat grondhoogte- en "alleen binnen"-regel vallen (surface + anti-clip blijven).
					bValidSpot = bFloor && (bFree || bGroundLevel) && !bOnPlaceable
							&& (bFree || CurrentDef.bAllowOutdoors || IsIndoors(PreviewLocation))
							&& !IsSpotBlocked(PreviewLocation, CurrentDef.BoxHalf, PreviewRotation.Yaw, CurrentDef.bIsPot);
				}
				} // einde else: niet-wandmount (vloer/plafond)
			}
		}
	}

	// Eigen ghost bijwerken (lokaal).
	const bool bShow = bPlacing && bAimHit;
	// Plaatsing-offset (root t.o.v. het trefpunt) — gelijk aan hoe het object straks gespawnd wordt.
	float ZOff = 0.f;
	if (CurrentDef.bIsLamp) { ZOff = -CurrentDef.BoxHalf.Z; }
	else if (CurrentDef.bIsDryRack) { ZOff = CurrentDef.bIsWallMount ? 0.f : CurrentDef.BoxHalf.Z; }
	else if (CurrentDef.bIsSink || CurrentDef.bIsPackBench || CurrentDef.bIsShelf) { ZOff = CurrentDef.BoxHalf.Z; }
	// pot / atm / generieke prop: root op de vloer (offset 0)

	const bool bUsePreviewActor = PreviewActor.IsValid();
	if (Ghost)
	{
		// Met een echt preview-model verbergen we de primitieve ghost; anders valt 'ie terug op de oude ghost.
		Ghost->SetVisibility(bShow && !bUsePreviewActor);
		if (bShow && !bUsePreviewActor)
		{
			float GhostZOff = CurrentDef.BoxHalf.Z;
			if (CurrentDef.bIsLamp) { GhostZOff = -CurrentDef.BoxHalf.Z; }
			else if (CurrentDef.bIsWallMount) { GhostZOff = 0.f; }
			Ghost->SetWorldLocationAndRotation(PreviewLocation + FVector(0.f, 0.f, GhostZOff), PreviewRotation);
		}
		if (GhostMID)
		{
			GhostMID->SetVectorParameterValue(TEXT("GhostColor"),
				bValidSpot ? FLinearColor(0.15f, 0.5f, 1.f, 1.f) : FLinearColor(1.f, 0.15f, 0.15f, 1.f));
		}
	}
	if (bUsePreviewActor)
	{
		AActor* P = PreviewActor.Get();
		P->SetActorHiddenInGame(!bShow);
		if (bShow)
		{
			P->SetActorLocationAndRotation(PreviewLocation + FVector(0.f, 0.f, ZOff), PreviewRotation);
			if (PreviewMID)
			{
				PreviewMID->SetVectorParameterValue(TEXT("GhostColor"),
					bValidSpot ? FLinearColor(0.15f, 0.5f, 1.f, 1.f) : FLinearColor(1.f, 0.15f, 0.15f, 1.f));
			}
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
				UWeedToast::Notify(-1, 2.5f, FColor::Orange, TEXT("Harvest the plant before picking up the pot."));
			}
			return;
		}
		// Geef de juiste pot-tier terug (of fallback Pot_Broken voor oude potten).
		ReturnItem = Pot->GetPotTier().IsNone() ? FName(TEXT("Pot_Broken")) : Pot->GetPotTier();
	}
	else if (const APlaceableProp* Prop = Cast<APlaceableProp>(Target))
	{
		ReturnItem = Prop->ItemId;
	}
	else if (const ADryingRack* Rack = Cast<ADryingRack>(Target))
	{
		// Eerst leeg laten drogen/oogsten voor je het rek oppakt (anders verlies je de batches).
		if (!Rack->IsEmpty())
		{
			if (GEngine) { UWeedToast::Notify(-1, 2.5f, FColor::Orange, TEXT("Empty the drying rack before picking it up.")); }
			return;
		}
		ReturnItem = Rack->RackTier;
	}
	else if (const APackBench* Bench = Cast<APackBench>(Target))
	{
		ReturnItem = Bench->BenchTier; // geen opgeslagen staat -> altijd oppakbaar
	}
	else if (const AStorageShelf* Shelf = Cast<AStorageShelf>(Target))
	{
		// Eerst leeghalen voor je het schap oppakt (anders verlies je de voorraad).
		if (Shelf->Contents.Num() > 0)
		{
			if (GEngine) { UWeedToast::Notify(-1, 2.5f, FColor::Orange, TEXT("Empty the shelf before picking it up.")); }
			return;
		}
		ReturnItem = Shelf->ShelfTier;
	}
	else if (Cast<AWaterSink>(Target))
	{
		ReturnItem = FName(TEXT("Sink"));
	}
	else if (Cast<ACeilingLamp>(Target))
	{
		ReturnItem = FName(TEXT("Lamp_Ceiling"));
	}
	else if (Cast<AAtm>(Target))
	{
		ReturnItem = FName(TEXT("Atm"));
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
		UWeedToast::Notify(-1, 2.f, FColor::Green, TEXT("Picked up."));
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
		float ZOff = 20.f;
		if (GetPlaceableDef(RepItemId, Def))
		{
			if (UStaticMesh* M = LoadObject<UStaticMesh>(nullptr, Def.MeshPath))
			{
				Ghost->SetStaticMesh(M);
			}
			Ghost->SetWorldScale3D(Def.MeshScale);
			HalfZ = Def.BoxHalf.Z;
			ZOff = Def.bIsLamp ? -HalfZ : (Def.bIsWallMount ? 0.f : HalfZ);
		}
		Ghost->SetWorldLocationAndRotation(RepLocation + FVector(0.f, 0.f, ZOff), FRotator(0.f, RepYaw, 0.f));
		if (GhostMID)
		{
			GhostMID->SetVectorParameterValue(TEXT("GhostColor"),
				bRepValid ? FLinearColor(0.15f, 0.5f, 1.f, 1.f) : FLinearColor(1.f, 0.15f, 0.15f, 1.f));
		}
	}
}

bool UBuildComponent::IsPickable(const AActor* A) const
{
	return A && (Cast<AGrowPlant>(A) || Cast<APlaceableProp>(A) || Cast<ADryingRack>(A)
		|| Cast<APackBench>(A) || Cast<AStorageShelf>(A) || Cast<AWaterSink>(A) || Cast<ACeilingLamp>(A)
		|| Cast<AAtm>(A));
}

bool UBuildComponent::IsIndoors(const FVector& FloorPoint) const
{
	if (!bIndoorsOnly) { return true; }
	const UWorld* World = GetWorld();
	if (!World) { return true; }

	// Trace recht omhoog: raken we een plafond/dak -> binnen. Buiten is er open lucht boven.
	// We checken NIET alleen het midden maar ook een paar punten eromheen: staat de plek net onder een
	// deuropening of trapgat, dan vangt een naburig punt het plafond alsnog -> geen valse "buiten".
	FCollisionQueryParams Params(SCENE_QUERY_STAT(WeedShopIndoorTrace), false);
	if (GetOwner()) { Params.AddIgnoredActor(GetOwner()); }
	static const FVector2D Offs[] = { {0.f, 0.f}, {55.f, 0.f}, {-55.f, 0.f}, {0.f, 55.f}, {0.f, -55.f} };
	for (const FVector2D& O : Offs)
	{
		const FVector Start = FloorPoint + FVector(O.X, O.Y, 25.f);
		const FVector End = Start + FVector(0.f, 0.f, CeilingTraceHeight);
		FHitResult Ceil;
		if (World->LineTraceSingleByChannel(Ceil, Start, End, ECC_Visibility, Params)) { return true; }
	}
	return false;
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

void UBuildComponent::RotatePlacement()
{
	if (bPlacing)
	{
		PlaceYawOffset = FMath::Fmod(PlaceYawOffset + 90.f, 360.f);
	}
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
	// Plafondlampen (plafond) en wand-mounts (muur) gebruiken geen vloer-gebaseerde checks -> overslaan.
	if (!Def.bIsLamp && !Def.bIsWallMount && IsSpotBlocked(Location, Def.BoxHalf, Rotation.Yaw, Def.bIsPot))
	{
		if (GEngine)
		{
			UWeedToast::Notify(-1, 2.f, FColor::Red, TEXT("Can't place there (blocked)."));
		}
		return;
	}
	// Alleen binnenshuis (tenzij dit placeable buiten mag, bv. de ATM) - in testing/sandbox vrij.
	// Lampen bewijzen "binnen" door een plafond te raken; wand-mounts (rekken) NIET -> die checken we ook.
	if (!Def.bIsLamp && !Def.bAllowOutdoors && !WorldFreeBuild(World) && !IsIndoors(Location))
	{
		if (GEngine)
		{
			UWeedToast::Notify(-1, 2.f, FColor::Orange, TEXT("You can only place things inside the house."));
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

	if (Def.bIsAtm)
	{
		// Geldautomaat: spawn een interactieve AAtm.
		World->SpawnActor<AAtm>(AAtm::StaticClass(), FTransform(Rotation, Location), SpawnParams);
	}
	else if (Def.bIsLamp)
	{
		// Plafondlamp: hangt ONDER het plafondpunt (Location = plafond-trefpunt).
		const FTransform LampTM(Rotation, Location - FVector(0.f, 0.f, Def.BoxHalf.Z));
		World->SpawnActor<ACeilingLamp>(ACeilingLamp::StaticClass(), LampTM, SpawnParams);
	}
	else if (Def.bIsSink)
	{
		// Gootsteen: mesh-pivot in het midden -> origin een halve hoogte omhoog.
		const FTransform SinkTM(Rotation, Location + FVector(0.f, 0.f, Def.BoxHalf.Z));
		World->SpawnActor<AWaterSink>(AWaterSink::StaticClass(), SinkTM, SpawnParams);
	}
	else if (Def.bIsDryRack)
	{
		// Droogrek hangt aan de muur: Location is al het MIDDEN (preview rekent de muur-offset al uit),
		// dus spawn precies daar. (Mesh-pivot zit in het midden.) Deferred zodat de tier klopt.
		const FTransform RackTM(Rotation, Def.bIsWallMount ? Location : Location + FVector(0.f, 0.f, Def.BoxHalf.Z));
		ADryingRack* Rack = World->SpawnActorDeferred<ADryingRack>(
			ADryingRack::StaticClass(), RackTM,
			GetOwner(), nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (Rack)
		{
			Rack->RackTier = ItemId;
			Rack->FinishSpawning(RackTM);
		}
	}
	else if (Def.bIsPackBench)
	{
		// Verpak-tafel: idem, mesh-pivot in het midden -> origin een halve hoogte omhoog.
		const FTransform BenchTM(Rotation, Location + FVector(0.f, 0.f, Def.BoxHalf.Z));
		APackBench* Bench = World->SpawnActorDeferred<APackBench>(
			APackBench::StaticClass(), BenchTM,
			GetOwner(), nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (Bench)
		{
			Bench->BenchTier = ItemId;
			Bench->FinishSpawning(BenchTM);
		}
	}
	else if (Def.bIsShelf)
	{
		// Opslag-schap: idem, mesh-pivot in het midden -> origin een halve hoogte omhoog.
		const FTransform ShelfTM(Rotation, Location + FVector(0.f, 0.f, Def.BoxHalf.Z));
		AStorageShelf* Shelf = World->SpawnActorDeferred<AStorageShelf>(
			AStorageShelf::StaticClass(), ShelfTM,
			GetOwner(), nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (Shelf)
		{
			Shelf->ShelfTier = ItemId;
			Shelf->FinishSpawning(ShelfTM);
		}
	}
	else if (Def.bIsPot)
	{
		// Deferred zodat de pot-tier al klopt vóór BeginPlay (bepaalt waterretentie/yield/uiterlijk).
		AGrowPlant* Pot = World->SpawnActorDeferred<AGrowPlant>(
			AGrowPlant::StaticClass(), FTransform(Rotation, Location),
			GetOwner(), nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (Pot)
		{
			Pot->PotTier = ItemId;
			Pot->FinishSpawning(FTransform(Rotation, Location));
		}
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
		UWeedToast::Notify(-1, 2.f, FColor::Green, TEXT("Placed."));
	}
}
