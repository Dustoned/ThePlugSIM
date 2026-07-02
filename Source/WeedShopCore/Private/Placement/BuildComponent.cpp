#include "Placement/BuildComponent.h"
#include "UI/WeedToast.h"

#include "WeedShopCore.h"
#include "Cultivation/GrowPlant.h"
#include "Cultivation/PotTypes.h" // GearUpgradeIndex -> ring tonen bij gear-upgrades
#include "Placement/PlaceableTypes.h"
#include "Placement/PlaceableProp.h"
#include "World/Atm.h"
#include "World/PackBench.h"
#include "World/StorageShelf.h"
#include "World/WaterSink.h"
#include "World/CeilingLamp.h"
#include "World/PackLightSwitch.h"
#include "World/ProcessorMachine.h"
#include "Cultivation/DryingRack.h"
#include "Inventory/InventoryComponent.h"
#include "Phone/PhoneClientComponent.h"
#include "World/CityDoor.h" // geen wand-mounts/objecten op deuren plaatsen
#include "Interaction/InteractionComponent.h"
#include "Game/WeedShopGameState.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/World.h"
#include "Engine/OverlapResult.h"
#include "EngineUtils.h"
#include "Misc/FileHelper.h" // Saved/BuildArea.txt lezen (speler-build-box)
#include "Misc/Paths.h"
#include "World/DoorRetrofitter.h"
#include "Engine/Engine.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PlayerController.h"
#include "Camera/CameraComponent.h"
#include "InputCoreTypes.h"
#include "Net/UnrealNetwork.h"

namespace
{
	// Testing/sandbox: overal plaatsen toegestaan (geen "indoors only"/grondhoogte-regel).
	bool WorldFreeBuild(const UWorld* W)
	{
		const AWeedShopGameState* GS = W ? W->GetGameState<AWeedShopGameState>() : nullptr;
		return GS && GS->IsFreeBuild();
	}

	// True als de actor een geplaatst object is waarop je NIET mag stapelen (alle placeable-types).
	// Wand-mounts (rekken) + plafondlampen worden apart afgehandeld; mikken op zo'n object telt ook als bezet.
	bool IsPlaceableActor(const AActor* A)
	{
		return A && (A->IsA(AGrowPlant::StaticClass()) || A->IsA(APlaceableProp::StaticClass())
			|| A->IsA(ADryingRack::StaticClass()) || A->IsA(AProcessorMachine::StaticClass())
			|| A->IsA(AStorageShelf::StaticClass()) || A->IsA(AAtm::StaticClass())
			|| A->IsA(APackBench::StaticClass()) || A->IsA(AWaterSink::StaticClass())
			|| A->IsA(ACeilingLamp::StaticClass()) || A->IsA(APackLightSwitch::StaticClass()));
	}

	// Overlapt de (geroteerde) box rond Center met een ANDER geplaatst object? Muren/vloeren tellen niet mee,
	// dus strak tegen de muur blijft prima - dit vangt alleen het door-elkaar-clippen van furniture (vooral
	// wand-mounts/lampen, die geen vloer-gebaseerde IsSpotBlocked-check hebben).
	bool OverlapsOtherPlaceable(const UWorld* World, const AActor* Ignore, const FVector& Center, const FVector& BoxHalf, const FQuat& Rot)
	{
		if (!World) { return false; }
		FCollisionObjectQueryParams ObjParams;
		ObjParams.AddObjectTypesToQuery(ECC_WorldStatic);
		ObjParams.AddObjectTypesToQuery(ECC_WorldDynamic);
		FCollisionQueryParams QP(SCENE_QUERY_STAT(WeedShopPlaceableOverlap), false);
		if (Ignore) { QP.AddIgnoredActor(Ignore); }
		const FCollisionShape Box = FCollisionShape::MakeBox(FVector(
			FMath::Max(2.f, BoxHalf.X - 2.f), FMath::Max(2.f, BoxHalf.Y - 2.f), FMath::Max(2.f, BoxHalf.Z - 2.f)));
		TArray<FOverlapResult> Overlaps;
		World->OverlapMultiByObjectType(Overlaps, Center, Rot, ObjParams, Box, QP);
		for (const FOverlapResult& R : Overlaps)
		{
			if (IsPlaceableActor(R.GetActor())) { return true; }
		}
		return false;
	}

	// Steekt een WAND-MOUNT-item (rug tegen de muur) met enig deel DOOR een HAAKSE / hoek-muur? De rug ligt al
	// strak tegen het geraakte muurvlak; het item kan echter zijdelings (in een hoek) door een dwarsmuur clippen.
	// We nemen alleen het VOORSTE stuk van de item-box (van de rug tot de voorkant), zodat de eigen draagmuur
	// NIET meetelt, maar houden de VOLLE breedte + hoogte aan zodat een dwarsmuur die de box snijdt WEL raakt.
	// bFaceX: het item heeft z'n diepte op lokale X (lichtschakelaar); anders op lokale Y (rek/schap/TV).
	bool WallItemClipsWall(const UWorld* World, const AActor* Ignore, const FVector& Center, const FVector& BoxHalf, const FQuat& Rot, bool bFaceX)
	{
		if (!World) { return false; }
		// D = muur-normaal de kamer in (langs de diepte-as van het item).
		const FVector D = Rot.RotateVector(bFaceX ? FVector(1.f, 0.f, 0.f) : FVector(0.f, 1.f, 0.f));
		const float DepthHalf = bFaceX ? BoxHalf.X : BoxHalf.Y; // halve diepte (rug -> voorkant)
		const float WidthHalf = bFaceX ? BoxHalf.Y : BoxHalf.X; // halve breedte (langs de muur)
		// Box = alleen de voorste helft van de diepte: back-vlak op de rug (Center - D*DepthHalf ligt op de draagmuur,
		// de box-achterkant valt op Center, dus DepthHalf van de draagmuur af). Volle breedte/hoogte behouden.
		const FVector CheckCenter = Center + D * (DepthHalf * 0.5f);
		// Hoogte-band iets ingekort (top+bodem) zodat een VLOER/PLAFOND waar het item tegenaan rust NIET meetelt;
		// een verticale dwarsmuur (~160cm hoog) snijdt de midden-band alsnog. Depth = alleen de voorste helft.
		const float HalfZBand = FMath::Max(2.f, BoxHalf.Z - 12.f);
		const FVector LocalHalf = bFaceX
			? FVector(DepthHalf * 0.5f, WidthHalf, HalfZBand)
			: FVector(WidthHalf, DepthHalf * 0.5f, HalfZBand);
		FCollisionObjectQueryParams ObjParams;
		ObjParams.AddObjectTypesToQuery(ECC_WorldStatic);
		FCollisionQueryParams QP(SCENE_QUERY_STAT(WeedShopWallClip), false);
		if (Ignore) { QP.AddIgnoredActor(Ignore); }
		const FCollisionShape ClipBox = FCollisionShape::MakeBox(FVector(
			FMath::Max(2.f, LocalHalf.X), FMath::Max(2.f, LocalHalf.Y), LocalHalf.Z));
		TArray<FOverlapResult> Hits;
		if (World->OverlapMultiByObjectType(Hits, CheckCenter, Rot, ObjParams, ClipBox, QP))
		{
			for (const FOverlapResult& H : Hits)
			{
				// Deur/deurblad telt niet als "muur waar je doorheen steekt" (aparte deur-filters vangen dat al).
				const AActor* HA = H.GetActor();
				if (HA && HA->IsA(ACityDoor::StaticClass())) { continue; }
				return true; // een WorldStatic-vlak (dwarsmuur/hoek) snijdt de voorkant van het item
			}
		}
		return false;
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
	// Speler-markers (build-box + no-build-zones) eenmalig inladen zodat de eerste placement meteen klopt en er
	// geen file-read-hitch bij placement-start is.
	RefreshBuildArea();
	RefreshNoBuildZones();
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
	// Lokale speler: gebruik de FP-camera RECHTSTREEKS. Deterministisch op host EN client; voorkomt dat
	// de client (waar GetPlayerViewPoint/Controller anders kan zijn) een verkeerd zichtpunt krijgt
	// waardoor de ghost/plaatsing op de hand-positie i.p.v. waar je mikt belandt.
	if (OwnerPawn->IsLocallyControlled())
	{
		if (const UCameraComponent* Cam = OwnerPawn->FindComponentByClass<UCameraComponent>())
		{
			OutLocation = Cam->GetComponentLocation();
			OutRotation = Cam->GetComponentRotation();
			return true;
		}
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
	{
		const FString IdS = ItemId.ToString();
		CurUpgradeKind = (GearUpgradeIndex(ItemId) >= 0) ? 1 : (IdS.StartsWith(TEXT("DryUp_")) ? 2 : (IdS.StartsWith(TEXT("ProcUp_")) ? 3 : 0));
	}
	bPlacingGear = (CurUpgradeKind != 0); // upgrade -> toon de bereik-/doel-ring
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

	// Platte effect-bereik-ring (alleen voor gear-upgrades): toont welke pots binnen bereik vallen.
	RangeRing = NewObject<UStaticMeshComponent>(GetOwner());
	if (RangeRing)
	{
		RangeRing->SetupAttachment(GetOwner()->GetRootComponent());
		RangeRing->RegisterComponent();
		RangeRing->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		RangeRing->SetCastShadow(false);
		RangeRing->SetAbsolute(true, true, true);
		if (UStaticMesh* Cyl = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder")))
		{
			RangeRing->SetStaticMesh(Cyl);
		}
		if (UMaterialInterface* GhostMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/_Project/Materials/M_PlacementGhost.M_PlacementGhost")))
		{
			RangeRingMID = UMaterialInstanceDynamic::Create(GhostMat, this);
			if (RangeRingMID)
			{
				RangeRingMID->SetVectorParameterValue(TEXT("GhostColor"), FLinearColor(0.2f, 0.9f, 0.4f, 1.f));
				RangeRing->SetMaterial(0, RangeRingMID);
			}
		}
		// Cylinder = 100cm diameter / 100cm hoog, gecentreerd. Straal 175cm -> diameter 350 -> schaal 3.5; plat.
		RangeRing->SetWorldScale3D(FVector(3.5f, 3.5f, 0.03f));
		RangeRing->SetVisibility(false);
	}

	// Heldere ring op de doel-pot (de pot die deze gear daadwerkelijk krijgt).
	TargetRing = NewObject<UStaticMeshComponent>(GetOwner());
	if (TargetRing)
	{
		TargetRing->SetupAttachment(GetOwner()->GetRootComponent());
		TargetRing->RegisterComponent();
		TargetRing->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		TargetRing->SetCastShadow(false);
		TargetRing->SetAbsolute(true, true, true);
		if (UStaticMesh* Cyl = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder")))
		{
			TargetRing->SetStaticMesh(Cyl);
		}
		if (UMaterialInterface* GhostMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/_Project/Materials/M_PlacementGhost.M_PlacementGhost")))
		{
			TargetRingMID = UMaterialInstanceDynamic::Create(GhostMat, this);
			if (TargetRingMID)
			{
				TargetRingMID->SetVectorParameterValue(TEXT("GhostColor"), FLinearColor(0.4f, 1.f, 0.3f, 1.f));
				TargetRing->SetMaterial(0, TargetRingMID);
			}
		}
		TargetRing->SetWorldScale3D(FVector(1.4f, 1.4f, 0.05f)); // ~140cm rond de doel-pot
		TargetRing->SetVisibility(false);
	}
}

void UBuildComponent::EnsureDoorMarks()
{
	if (DoorMarks.Num() > 0 || !GetOwner()) { return; }
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	UMaterialInterface* GhostMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/_Project/Materials/M_PlacementGhost.M_PlacementGhost"));
	for (int32 i = 0; i < 64; ++i) // pool voor het no-go-raster (7x7 cellen + ruimte)
	{
		UStaticMeshComponent* M = NewObject<UStaticMeshComponent>(GetOwner());
		if (!M) { continue; }
		M->SetupAttachment(GetOwner()->GetRootComponent());
		M->RegisterComponent();
		M->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		M->SetCastShadow(false);
		M->SetAbsolute(true, true, true);
		if (Cube) { M->SetStaticMesh(Cube); }
		UMaterialInstanceDynamic* MID = GhostMat ? UMaterialInstanceDynamic::Create(GhostMat, this) : nullptr;
		if (MID) { MID->SetVectorParameterValue(TEXT("GhostColor"), FLinearColor(1.f, 0.18f, 0.18f, 1.f)); M->SetMaterial(0, MID); }
		M->SetVisibility(false);
		DoorMarks.Add(M);
		DoorMarkMIDs.Add(MID);
	}
}

void UBuildComponent::RefreshDoorCache(const FVector& Center)
{
	CachedDoorPositions.Reset();
	LastDoorCachePos = Center;
	UWorld* W = GetWorld();
	if (!W) { return; }
	// Goedkope ruimtelijke query i.p.v. alle actors aflopen: pak alle collidende objecten binnen ~16 m en
	// houd de STATIC-MESH-componenten over waarvan de mesh-naam "Door" bevat (maar geen "DoorFrame"/"Doorway").
	// Zo vangen we ZOWEL omgezette ACityDoor-bladen ALS nog-niet-omgezette apartment-deur-meshes (los van class).
	TArray<FOverlapResult> Ov;
	FCollisionObjectQueryParams Obj;
	Obj.AddObjectTypesToQuery(ECC_WorldStatic);
	Obj.AddObjectTypesToQuery(ECC_WorldDynamic);
	FCollisionQueryParams Q(SCENE_QUERY_STAT(DoorCache), false);
	if (GetOwner()) { Q.AddIgnoredActor(GetOwner()); }
	if (W->OverlapMultiByObjectType(Ov, Center, FQuat::Identity, Obj, FCollisionShape::MakeSphere(1600.f), Q))
	{
		for (const FOverlapResult& R : Ov)
		{
			const UStaticMeshComponent* C = Cast<UStaticMeshComponent>(R.GetComponent());
			if (!C || !C->GetStaticMesh()) { continue; }
			const FString N = C->GetStaticMesh()->GetName();
			if (!N.Contains(TEXT("Door")) || N.Contains(TEXT("DoorFrame")) || N.Contains(TEXT("Doorway"))) { continue; }
			// MIDDEN van het deurblad (bounds-center) i.p.v. de component-origin - die zit op het SCHARNIER,
			// naast de opening, waardoor de marker naast de deur belandde. Z = onderkant bounds = de vloer.
			const FBox B = C->Bounds.GetBox();
			const FVector Ctr = B.GetCenter();
			const FVector Pos(Ctr.X, Ctr.Y, B.Min.Z);
			// ALLEEN de deuren van JOUW eigen huis (niet elke deur in de gang/bij de buren). Test een punt net
			// BINNEN de opening (richting waar je staat te plaatsen): een deur in je eigen muur valt dan binnen
			// je huis-grens, een buur-/gang-deur niet. Op de plaats-Z (die ligt sowieso binnen je huis).
			FVector TestPt(Ctr.X, Ctr.Y, Center.Z);
			FVector Toward = Center - TestPt; Toward.Z = 0.f;
			if (!Toward.IsNearlyZero()) { TestPt += Toward.GetSafeNormal() * 55.f; }
			if (!IsInOwnedHome(TestPt)) { continue; }
			bool bDup = false;
			for (const FVector& P : CachedDoorPositions) { if (FVector::DistSquared2D(P, Pos) < 80.f * 80.f) { bDup = true; break; } }
			if (!bDup) { CachedDoorPositions.Add(Pos); }
		}
	}
}

bool UBuildComponent::UpdateDoorwayMarkers(bool bShow, const FVector& FootCenter, const FVector& FootHalf, float Yaw)
{
	UWorld* W = GetWorld();
	if (!bShow || !W) { return false; }
	// Deur-cache verversen zodra je ~1,5 m bent verplaatst (deuren staan stil -> geen scan per tick).
	if (CachedDoorPositions.Num() == 0 || FVector::Dist2D(FootCenter, LastDoorCachePos) > 150.f) { RefreshDoorCache(FootCenter); }
	// Blokkeer zodra de FOOTPRINT van je plaatsing in een deur-zone valt (niet alleen het midden -> brede
	// objecten hangen niet meer over de opening). De rode vlakken worden door UpdateNoGoGrid getekend (die
	// hergebruikt de DoorMarks-pool) - hier alleen de boolean voor de ghost.
	return DoorBlocksCell(FootCenter, FootHalf, Yaw);
}

bool UBuildComponent::DoorBlocksCell(const FVector& Cell, const FVector& BoxHalf, float Yaw) const
{
	const float HZone = 60.f; // halve no-go-zone rond een deur-opening (cm): de drempel/zwaai vrijhouden, niet meer
	// Effectieve XY-half-extents na de yaw-draai (zelfde 90-graden-swap-idioom als de shift-snap, r~747-749).
	const bool bSwap = FMath::IsNearlyEqual(FMath::Fmod(FMath::Abs(Yaw), 180.f), 90.f, 45.f);
	const bool bAxis = FMath::IsNearlyZero(FMath::Fmod(FMath::Abs(Yaw), 90.f), 5.f); // ~0/90/180/270 -> as-uitgelijnd
	float EffHX, EffHY;
	if (bAxis) { EffHX = bSwap ? BoxHalf.Y : BoxHalf.X; EffHY = bSwap ? BoxHalf.X : BoxHalf.Y; }
	else { EffHX = EffHY = FMath::Max(BoxHalf.X, BoxHalf.Y); } // niet-90°-yaw: conservatief (grootste extent op beide assen)
	for (const FVector& DL : CachedDoorPositions)
	{
		if (FMath::Abs(DL.X - Cell.X) < HZone + EffHX && FMath::Abs(DL.Y - Cell.Y) < HZone + EffHY && FMath::Abs(Cell.Z - DL.Z) < 220.f) { return true; }
	}
	return false;
}

bool UBuildComponent::IsPlacementValidAt(const FVector& Loc, float Yaw, float& FloorZOut, bool& bHasFloorOut) const
{
	bHasFloorOut = false;
	FloorZOut = Loc.Z;
	UWorld* W = GetWorld();
	if (!W) { return false; }
	// Zelfde down-trace als de ghost (regels 698-707): zoek de vloer onder deze XY.
	FCollisionQueryParams Params(SCENE_QUERY_STAT(WeedShopGridSample), false);
	if (GetOwner()) { Params.AddIgnoredActor(GetOwner()); }
	FHitResult Down;
	const FVector DStart(Loc.X, Loc.Y, Loc.Z + 250.f);
	const FVector DEnd(Loc.X, Loc.Y, Loc.Z - 250.f);
	if (!W->LineTraceSingleByChannel(Down, DStart, DEnd, ECC_Visibility, Params)) { return false; }
	bHasFloorOut = true;
	FloorZOut = Down.ImpactPoint.Z;
	const FVector P(Loc.X, Loc.Y, Down.ImpactPoint.Z);
	const bool bFloor = Down.ImpactNormal.Z > 0.7f;
	const bool bOnPlaceable = IsPlaceableActor(Down.GetActor());
	// Exact de ground-floor ghost-regel (727-729), zonder de bGroundLevel-term (het raster sampelt al op de vloer).
	return bFloor && !bOnPlaceable
		&& (CurrentDef.bAllowOutdoors || IsInOwnedHome(P))
		&& !IsSpotBlocked(P, CurrentDef.BoxHalf, Yaw, CurrentDef.bIsPot);
}

void UBuildComponent::UpdateNoGoGrid(bool bShow, const FVector& Center, float Yaw)
{
	EnsureDoorMarks();
	auto HideAll = [this]() { for (UStaticMeshComponent* M : DoorMarks) { if (M) { M->SetVisibility(false); } } LastGridPos = FVector(1e9f); };
	UWorld* W = GetWorld();
	if (!bShow || !W) { HideAll(); return; }
	// Buiten je eigen huis (en niet outdoors-toegestaan)? Niks tonen.
	if (!CurrentDef.bAllowOutdoors && !IsInOwnedHome(Center)) { HideAll(); return; }
	// Deur-cache warm houden (deuren staan stil -> ververs alleen als je ~1,5 m bent verplaatst).
	if (CachedDoorPositions.Num() == 0 || FVector::Dist2D(Center, LastDoorCachePos) > 150.f) { RefreshDoorCache(Center); }
	// Posities zitten VAST op de deuren (volgen je blik niet). Geldigheid maar af en toe hersamplen (geen per-tick).
	const float Now = W->GetTimeSeconds();
	const bool bRot = FMath::Abs(FMath::FindDeltaAngleDegrees(LastGridYaw, Yaw)) > 10.f;
	if (!bRot && FVector::Dist2D(Center, LastGridPos) < 60.f && (Now - LastGridTime) < 0.3f && (Now - LastGridTime) >= 0.f) { return; }
	LastGridPos = Center; LastGridYaw = Yaw; LastGridTime = Now;

	// Rond ELKE deur in de buurt een klein raster (vast op de deur). Kleur ALLEEN de cellen die de echte validatie
	// afkeurt EN op vloer-hoogte liggen. Zo zit het rood exact waar je niet mag plaatsen - de deuropening - ook als
	// de deur-positie wat ruw is (we tonen alleen wat de check echt blokkeert). Muren/raam regelt de ghost zelf al.
	static const float Offs[4] = { -90.f, -30.f, 30.f, 90.f }; // 4x4 raster, ±90cm rond de deur
	const float Step = GridSize; // 60cm
	FCollisionQueryParams TP(SCENE_QUERY_STAT(NoGoFloor), false);
	if (GetOwner()) { TP.AddIgnoredActor(GetOwner()); }
	int32 mi = 0;
	for (const FVector& DL : CachedDoorPositions)
	{
		if (mi >= DoorMarks.Num()) { break; }
		if (FVector::Dist2D(DL, Center) > 800.f) { continue; } // alleen deuren dichtbij
		for (float oy : Offs)
		for (float ox : Offs)
		{
			if (mi >= DoorMarks.Num()) { break; }
			const FVector CellXY(DL.X + ox, DL.Y + oy, DL.Z);
			// Vloer onder deze cel zoeken.
			FHitResult Down;
			const bool bHasFloor = W->LineTraceSingleByChannel(Down, FVector(CellXY.X, CellXY.Y, DL.Z + 255.f), FVector(CellXY.X, CellXY.Y, DL.Z - 255.f), ECC_Visibility, TP);
			const float FloorZ = bHasFloor ? Down.ImpactPoint.Z : DL.Z;
			const FVector P(CellXY.X, CellXY.Y, FloorZ);
			// ALLEEN het echte deur/obstakel tonen (deurblad/muur via IsSpotBlocked, of de smalle deur-zone) - NIET
			// de huis-grens. Zo blijft open bouwbare vloer schoon; het rood zit op het deur-obstakel zelf.
			const bool bObstacle = IsSpotBlocked(P, CurrentDef.BoxHalf, Yaw, CurrentDef.bIsPot) || DoorBlocksCell(P, CurrentDef.BoxHalf, Yaw);
			// Alleen vloer-niveau (niet een cel die op een muur-bovenkant landt -> die gaf hoge zwevende blokjes).
			const bool bPaint = bHasFloor && FMath::Abs(FloorZ - DL.Z) < 60.f && bObstacle;
			if (UStaticMeshComponent* M = DoorMarks[mi])
			{
				if (bPaint)
				{
					M->SetWorldLocationAndRotation(FVector(CellXY.X, CellXY.Y, FloorZ + 15.f), FRotator::ZeroRotator);
					M->SetWorldScale3D(FVector(Step / 100.f * 0.9f, Step / 100.f * 0.9f, 0.30f));
					M->SetVisibility(true);
				}
				else { M->SetVisibility(false); }
			}
			++mi;
		}
	}
	for (; mi < DoorMarks.Num(); ++mi) { if (DoorMarks[mi]) { DoorMarks[mi]->SetVisibility(false); } }
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
	else if (Def.bIsProcessor)  { if (AProcessorMachine* M = Cast<AProcessorMachine>(Deferred(AProcessorMachine::StaticClass()))) { M->MachineTier = ItemId; M->FinishSpawning(TM); A = M; } }
	else if (Def.bIsSink)       { A = W->SpawnActor<AWaterSink>(AWaterSink::StaticClass(), TM); }
	else if (Def.bIsLamp)       { A = W->SpawnActor<ACeilingLamp>(ACeilingLamp::StaticClass(), TM); }
	else if (Def.bIsAtm)        { A = W->SpawnActor<AAtm>(AAtm::StaticClass(), TM); }
	else if (Def.bIsSafe)       { if (AStorageShelf* S = Cast<AStorageShelf>(Deferred(AStorageShelf::StaticClass()))) { S->ShelfTier = ItemId; S->FinishSpawning(TM); A = S; } } // kluis = opslag-kist-tier (item-opslag)
	else                        { if (APlaceableProp* P = Cast<APlaceableProp>(Deferred(APlaceableProp::StaticClass()))) { P->ItemId = ItemId; P->FinishSpawning(TM); A = P; } }

	if (!A) { return; }

	// LOKAAL/COSMETISCH: de preview mag NOOIT repliceren. De gespawnde class (APlaceableProp e.d.) heeft
	// bReplicates=true, dus zonder dit ziet je co-op-partner jouw preview als een SOLIDE object op de grond
	// (het ghost-materiaal wordt alleen lokaal gezet en repliceert niet). De partner-preview loopt via
	// UpdateRemoteGhost (gerepliceerde staat), niet via de preview-actor zelf.
	A->SetReplicates(false);

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
	bPlacingGear = false;
	CurUpgradeKind = 0;
	SnapTarget = nullptr;
	if (Ghost)
	{
		Ghost->SetVisibility(false);
	}
	if (RangeRing)
	{
		RangeRing->SetVisibility(false);
	}
	if (TargetRing)
	{
		TargetRing->SetVisibility(false);
	}
	for (UStaticMeshComponent* M : DoorMarks) { if (M) { M->SetVisibility(false); } } // no-go-raster verbergen
	LastGridPos = FVector(1e9f);
	DestroyPreview();
}

AActor* UBuildComponent::FindUpgradeTarget(int32 Kind, const FVector& Near) const
{
	if (!GetWorld()) { return nullptr; }
	AActor* Best = nullptr; float BestSq = FMath::Square(220.f); // mik binnen ~2,2m van een geldig object
	auto Consider = [&](AActor* A)
	{
		if (!IsValid(A)) { return; }
		const FVector L = A->GetActorLocation();
		if (FMath::Abs(L.Z - Near.Z) > 300.f) { return; }
		const float D = FVector::DistSquared2D(L, Near);
		if (D <= BestSq) { BestSq = D; Best = A; }
	};
	if (Kind == 1)      { for (TActorIterator<AGrowPlant> It(GetWorld()); It; ++It)        { Consider(*It); } }
	else if (Kind == 2) { for (TActorIterator<ADryingRack> It(GetWorld()); It; ++It)       { Consider(*It); } }
	else if (Kind == 3) { for (TActorIterator<AProcessorMachine> It(GetWorld()); It; ++It) { Consider(*It); } }
	return Best;
}

void UBuildComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// Speler-markers (Saved/BuildArea.txt + MarkedSpots.txt) herlezen: de build-box (waar je WEL mag) + de no-build-
	// zones (deuropeningen). ALLEEN tijdens plaatsen, en gegate op 2s -> geen file-reads als je gewoon rondloopt,
	// dus geen onnodige tick-kosten of spikes. Eenmalig al ingeladen in BeginPlay, dus de eerste placement-frame klopt.
	if (bPlacing)
	{
		BuildAreaTimer -= DeltaTime;
		if (BuildAreaTimer <= 0.f) { BuildAreaTimer = 2.f; RefreshBuildArea(); RefreshNoBuildZones(); }
	}

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
			// Negeer ook alles wat aan de speler vastzit (vastgehouden item-visual e.d.), zodat de trace
			// niet op de handpositie blijft hangen.
			{
				TArray<AActor*> Attached;
				GetOwner()->GetAttachedActors(Attached);
				Params.AddIgnoredActors(Attached);
			}

			FHitResult Hit;
			bAimHit = GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params);

			// BUILDING-TOOL (bouw-onderdelen): altijd grid-snap, ook in de lucht/void te plaatsen
			// (geen raak-punt nodig), Z snapt op de verdieping van de SPELER, plafonds 320 erboven.
			if (CurrentDef.bIsStructure)
			{
				FVector P = bAimHit ? Hit.ImpactPoint : End;
				// Verdieping van de speler: beach-map verdiepingen (50, dan 480 + n*350); de speler
				// staat zelf altijd op een echte vloer, dus dit snapt betrouwbaar.
				const float Feet = OwnerPawn->GetActorLocation().Z - 88.f;
				float StoreyZ;
				if (FMath::Abs(Feet - 50.f) < 215.f) { StoreyZ = 50.f; }
				else { StoreyZ = 480.f + 350.f * FMath::RoundToFloat((Feet - 480.f) / 350.f); }
				P.X = FMath::GridSnap(P.X, 50.f);
				P.Y = FMath::GridSnap(P.Y, 50.f);
				P.Z = StoreyZ + (CurrentDef.bIsCeilingPiece ? 320.f : 0.f);
				PreviewLocation = P;
				PreviewRotation = FRotator(0.f, FMath::GridSnap<float>(ViewRot.Yaw + PlaceYawOffset, 90.f), 0.f);
				bAimHit = true;     // preview altijd tonen
				bValidSpot = true;  // vrij bouwen: naast/tegen andere stukken aan mag gewoon
			}
			else if (bAimHit)
			{
				PreviewLocation = Hit.ImpactPoint;
				PreviewRotation = FRotator(0.f, ViewRot.Yaw + PlaceYawOffset, 0.f); // kijkrichting + handmatige R-draai
				float FloorNormalZ = Hit.ImpactNormal.Z;
				// Mik je (onder welke hoek dan ook) op een geplaatst object -> nooit geldig (geen stapelen).
				bool bOnPlaceable = IsPlaceableActor(Hit.GetActor());
				// Op een DEUR (open of dicht) mag je niks plaatsen - die telt niet als muur/vloer.
				const bool bOnDoor = Hit.GetActor() && Hit.GetActor()->IsA(ACityDoor::StaticClass());

					if (CurrentDef.bIsWallMount)
					{
						// Wand-mount (droogrek): rug tegen een VERTICALE muur, voorkant de kamer in.
						const FVector N = Hit.ImpactNormal;
						const bool bWall = FMath::Abs(N.Z) < 0.4f; // verticaal vlak
						FVector D = FVector(N.X, N.Y, 0.f).GetSafeNormal(); // horizontale muur-normaal (de kamer in)
						if (D.IsNearlyZero()) { D = FVector(1.f, 0.f, 0.f); }
						// De lightswitch-plaat staat dun op LOKALE X (knop op +X); rek/TV hebben hun diepte op +Y.
						// Richt de juiste as langs de muur-normaal, zodat de knop/voorkant de kamer in (naar de speler) wijst.
						const bool bFaceX = CurrentDef.bIsLightSwitch;
						PreviewRotation = FRotator(0.f, FMath::RadiansToDegrees(FMath::Atan2(D.Y, D.X)) - (bFaceX ? 0.f : 90.f), 0.f);
						// Midden = trefpunt + normaal * halve diepte (rug strak tegen de muur). Hoogte = waar je mikt.
						FVector Center = Hit.ImpactPoint + D * (bFaceX ? CurrentDef.BoxHalf.X : CurrentDef.BoxHalf.Y);
						// Shift: snap langs de muur (horizontale tangent) en in hoogte op het raster.
						const APlayerController* PCw = Cast<APlayerController>(OwnerPawn->GetController());
						if (PCw && (PCw->IsInputKeyDown(EKeys::LeftShift) || PCw->IsInputKeyDown(EKeys::RightShift)) && GridSize > 1.f)
						{
							const FVector Tang(-D.Y, D.X, 0.f); // horizontale richting langs de muur
							const float Along = FVector::DotProduct(Center, Tang);
							// Snap langs de muur op de BREEDTE van het rek (lokale X = langs de muur) zodat twee
							// rekken naast elkaar FLUSH aansluiten zonder gat - niet op een los wereld-raster.
							const float WallStep = FMath::Max(20.f, CurrentDef.BoxHalf.X * 2.f);
							Center += Tang * (FMath::GridSnap<float>(Along, WallStep) - Along);
							const float HeightStep = FMath::Max(20.f, CurrentDef.BoxHalf.Z * 2.f); // verticaal flush
							Center.Z = FMath::GridSnap<float>(Center.Z, HeightStep);
						}
						PreviewLocation = Center;
						// Geraakte mesh-naam: een RAAM (glas) is geen geldige wand-mount-drager, en een RAUWE
						// apartment-deur-mesh (nog niet omgezet naar ACityDoor) telt ook als deur. Zelfde deur-filter
						// als RefreshDoorCache (r~319): "Door" maar niet "DoorFrame"/"Doorway" (kozijn/opening zelf).
						const UStaticMeshComponent* HC = Cast<UStaticMeshComponent>(Hit.GetComponent());
						const FString HN = (HC && HC->GetStaticMesh()) ? HC->GetStaticMesh()->GetName() : FString();
						const bool bOnWindow = HN.Contains(TEXT("Glass")) || HN.Contains(TEXT("Window"));
						const bool bOnDoorMesh = bOnDoor || (HN.Contains(TEXT("Door")) && !HN.Contains(TEXT("DoorFrame")) && !HN.Contains(TEXT("Doorway")));
						// Wand-mount (rek/lamp/TV) mag alleen op een MUUR, niet op een deur/raam/ander object, niet DOOR
						// een ander object heen, en BINNEN je eigen huis. De huis-check pakt een punt iets DIEPER de kamer
						// in (de muur ligt op de build-box-rand, dus de rug-positie valt anders nét buiten en weigert 'ie
						// onterecht). Plus: bij rood de ECHTE reden tonen i.p.v. een oude "Aim at the floor"-hint.
						const bool bWmOverlap = OverlapsOtherPlaceable(GetWorld(), GetOwner(), PreviewLocation, CurrentDef.BoxHalf, PreviewRotation.Quaternion());
						const bool bWmHome = CurrentDef.bAllowOutdoors || IsInOwnedHome(Hit.ImpactPoint + D * (CurrentDef.BoxHalf.Y + 40.f));
						// Steekt de VOORKANT van het item door een HAAKSE / hoek-muur (WorldStatic)? Dan ongeldig (rood).
						const bool bWmClip = WallItemClipsWall(GetWorld(), GetOwner(), PreviewLocation, CurrentDef.BoxHalf, PreviewRotation.Quaternion(), bFaceX);
						bValidSpot = bWall && !bOnPlaceable && !bOnDoorMesh && !bOnWindow && !bWmOverlap && !bWmClip && bWmHome;
						if (!bValidSpot)
						{
							PlacementHint = !bWall ? TEXT("Aim at a wall")
								: bOnDoorMesh ? TEXT("Not on a door")
								: bOnWindow ? TEXT("Not on a window")
								: bOnPlaceable ? TEXT("Can't place on top of that")
								: bWmClip ? TEXT("Too close to a corner or wall")
								: !bWmHome ? TEXT("Only inside your own home")
								: TEXT("Too close to another object");
						}
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
								// Kleine marge van de muur af zodat de cel die het DICHTST bij de muur ligt ALTIJD
								// plaatsbaar blijft (trim/dikkere collision clipt anders net -> rood). Zo dicht mogelijk
								// tegen de muur, maar nooit ongeldig.
								const float WallGap = 4.f;
								float OriginX = 0.f;
								if (bX1 && (!bX2 || HX1.Distance <= HX2.Distance)) OriginX = HX1.ImpactPoint.X - EffHX - WallGap;
								else if (bX2) OriginX = HX2.ImpactPoint.X + EffHX + WallGap;
								PreviewLocation.X = OriginX + FMath::GridSnap<float>(PreviewLocation.X - OriginX, GridSize);

								const bool bY1 = GetWorld()->LineTraceSingleByChannel(HY1, TS, TS + FVector(0, 1500.f, 0), ECC_Visibility, Params);
								const bool bY2 = GetWorld()->LineTraceSingleByChannel(HY2, TS, TS + FVector(0, -1500.f, 0), ECC_Visibility, Params);
								float OriginY = 0.f;
								if (bY1 && (!bY2 || HY1.Distance <= HY2.Distance)) OriginY = HY1.ImpactPoint.Y - EffHY - WallGap;
								else if (bY2) OriginY = HY2.ImpactPoint.Y + EffHY + WallGap;
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
							bOnPlaceable = IsPlaceableActor(DownHit.GetActor());
						}
					}
				}

				// Alleen op grondniveau plaatsen: niet bovenop een pot/tafel/ander object
				// (dat ligt hoger dan je voeten). Plus vlakke vloer, niet op een pot, genoeg ruimte.
				if (CurrentDef.bIsLamp)
				{
					// Plafondlamp: geldig op een PLAFOND (omlaag-wijzend vlak), alleen in je EIGEN woning, en
					// niet DOOR een andere lamp/object heen.
					bValidSpot = (FloorNormalZ < -0.4f) && !bOnPlaceable
						&& !OverlapsOtherPlaceable(GetWorld(), GetOwner(), PreviewLocation, CurrentDef.BoxHalf, PreviewRotation.Quaternion())
						&& (CurrentDef.bAllowOutdoors || IsInOwnedHome(PreviewLocation));
				}
				else
				{
					// Rand-cel strakker/blauw: zit de pivot zo dicht tegen een muur dat de item-rand er net
					// doorheen clipt (trim/dikkere collision -> onnodig rood), duw 'm net genoeg NAAR BINNEN zodat
					// de rand WallGap (4cm) van de muur af blijft. Zelfde marge als de shift-tak (r~781). Puur
					// preview-positie (client-side); IsSpotBlocked bepaalt daarna nog steeds geldig/ongeldig.
					{
						const bool bSwapW = FMath::IsNearlyEqual(FMath::Fmod(FMath::Abs(PreviewRotation.Yaw), 180.f), 90.f, 45.f);
						const float EffHX = bSwapW ? CurrentDef.BoxHalf.Y : CurrentDef.BoxHalf.X;
						const float EffHY = bSwapW ? CurrentDef.BoxHalf.X : CurrentDef.BoxHalf.Y;
						const float WallGap = 4.f;
						const FVector TS(PreviewLocation.X, PreviewLocation.Y, PreviewLocation.Z + 30.f);
						FHitResult HX1, HX2, HY1, HY2;
						const bool bX1 = GetWorld()->LineTraceSingleByChannel(HX1, TS, TS + FVector(1500.f, 0, 0), ECC_Visibility, Params);
						const bool bX2 = GetWorld()->LineTraceSingleByChannel(HX2, TS, TS + FVector(-1500.f, 0, 0), ECC_Visibility, Params);
						if (bX1 && (!bX2 || HX1.Distance <= HX2.Distance)) { const float Lim = HX1.ImpactPoint.X - EffHX - WallGap; if (PreviewLocation.X > Lim) { PreviewLocation.X = Lim; } }
						else if (bX2) { const float Lim = HX2.ImpactPoint.X + EffHX + WallGap; if (PreviewLocation.X < Lim) { PreviewLocation.X = Lim; } }
						const bool bY1 = GetWorld()->LineTraceSingleByChannel(HY1, TS, TS + FVector(0, 1500.f, 0), ECC_Visibility, Params);
						const bool bY2 = GetWorld()->LineTraceSingleByChannel(HY2, TS, TS + FVector(0, -1500.f, 0), ECC_Visibility, Params);
						if (bY1 && (!bY2 || HY1.Distance <= HY2.Distance)) { const float Lim = HY1.ImpactPoint.Y - EffHY - WallGap; if (PreviewLocation.Y > Lim) { PreviewLocation.Y = Lim; } }
						else if (bY2) { const float Lim = HY2.ImpactPoint.Y + EffHY + WallGap; if (PreviewLocation.Y < Lim) { PreviewLocation.Y = Lim; } }
					}
					const bool bFree = WorldFreeBuild(GetWorld());
					const bool bFloor = FloorNormalZ > 0.7f;
					const float FeetZ = OwnerPawn->GetActorLocation().Z - OwnerPawn->GetSimpleCollisionHalfHeight();
					const bool bGroundLevel = FMath::Abs(PreviewLocation.Z - FeetZ) < 30.f;
					const bool bHome = CurrentDef.bAllowOutdoors || IsInOwnedHome(PreviewLocation);
					const bool bBlocked = IsSpotBlocked(PreviewLocation, CurrentDef.BoxHalf, PreviewRotation.Yaw, CurrentDef.bIsPot);
					// Vrij bouwen: laat grondhoogte- en "indoors only"-regel vallen (surface + anti-clip blijven).
					bValidSpot = bFloor && (bFree || bGroundLevel) && !bOnPlaceable && bHome && !bBlocked;
					// Concrete reden voor de popup-hint i.p.v. altijd "alleen in je huis".
					if (bValidSpot) { PlacementHint.Reset(); }
					else
					{
						PlacementHint = !bFloor ? TEXT("Aim at the floor")
							: bOnPlaceable ? TEXT("Can't place on top of that")
							: !(bFree || bGroundLevel) ? TEXT("Aim at floor level (not up on something)")
							: !bHome ? TEXT("Only inside your own home")
							: TEXT("In the way - too close to a wall, door or object");
					}
				}
				} // einde else: niet-wandmount (vloer/plafond)
			}
		}
	}

	// Upgrades MOETEN op hun object snappen (pot/rek/machine). Geen geldig doel onder de cursor -> rood,
	// niet plaatsbaar. Wel een doel -> de upgrade kleeft aan de speler-kant van dat object.
	SnapTarget = nullptr;
	if (bPlacing && CurUpgradeKind != 0)
	{
		AActor* Target = bAimHit ? FindUpgradeTarget(CurUpgradeKind, PreviewLocation) : nullptr;
		if (Target)
		{
			const FVector TL = Target->GetActorLocation();
			// Op de VLOER (niet op het aim-punt -> niet meer zweven). Voet-hoogte van de speler = de vloer.
			const float FeetZ = OwnerPawn->GetActorLocation().Z - OwnerPawn->GetSimpleCollisionHalfHeight();
			// Footprint van het doel (horizontaal) om de upgrade buiten de rand te zetten (geen clip).
			FVector TOrigin, TExt; Target->GetActorBounds(true, TOrigin, TExt);
			const float TargetRad = FMath::Max(TExt.X, TExt.Y);
			const bool bTent = PlacingItemId.ToString().StartsWith(TEXT("Gear_Tent"));
			if (bTent)
			{
				// Tent gaat OVER de pot: gecentreerd, op de vloer. Vrij draaien met R.
				PreviewLocation = FVector(TL.X, TL.Y, FeetZ);
				PreviewRotation = FRotator(0.f, PlaceYawOffset, 0.f);
			}
			else
			{
				// Andere upgrades: net buiten de rand van het object, aan de speler-kant, op de vloer.
				FVector Dir = OwnerPawn->GetActorLocation() - TL; Dir.Z = 0.f; Dir = Dir.GetSafeNormal();
				if (Dir.IsNearlyZero()) { Dir = FVector(1.f, 0.f, 0.f); }
				const float Edge = TargetRad + FMath::Max(CurrentDef.BoxHalf.X, CurrentDef.BoxHalf.Y) + 4.f;
				PreviewLocation = FVector(TL.X, TL.Y, FeetZ) + Dir * Edge;
				// Kijkt standaard naar het object, plus je eigen R-draai (90-graden stappen).
				PreviewRotation = FRotator(0.f, FMath::RadiansToDegrees(FMath::Atan2(-Dir.Y, -Dir.X)) + PlaceYawOffset, 0.f);
			}
			bValidSpot = true;
			SnapTarget = Target;
		}
		else { bValidSpot = false; }
	}

	// Eigen ghost bijwerken (lokaal).
	const bool bShow = bPlacing && bAimHit;
	// DEUR-OPENING vrijhouden: valt je plaatsing in de smalle deur-zone (drempel/zwaai, ~60cm rond de deur), dan
	// ongeldig -> de ghost wordt rood. Géén los rood raster meer (gaf bij open deuren te veel/verkeerd); alleen de
	// ghost + de hint. Niet voor de building-tool (structures).
	if (bShow && !CurrentDef.bIsStructure && (IsInNoBuildZone(PreviewLocation) || UpdateDoorwayMarkers(true, PreviewLocation, CurrentDef.BoxHalf, PreviewRotation.Yaw)))
	{
		bValidSpot = false;
		PlacementHint = TEXT("Keep the doorway clear");
	}
	UpdateNoGoGrid(false, PreviewLocation, PreviewRotation.Yaw); // oude raster-cellen verborgen houden
	// Plaatsing-offset (root t.o.v. het trefpunt) — gelijk aan hoe het object straks gespawnd wordt.
	float ZOff = 0.f;
	if (CurrentDef.bIsLamp) { ZOff = -CurrentDef.BoxHalf.Z; }
	else if (CurrentDef.bIsDryRack) { ZOff = CurrentDef.bIsWallMount ? 0.f : CurrentDef.BoxHalf.Z; }
	// Wand-mount (bv. het schap): Location is al het MIDDEN (preview rekent de muur-offset al uit) -> geen extra Z.
	else if (CurrentDef.bIsSink || CurrentDef.bIsPackBench || CurrentDef.bIsShelf || CurrentDef.bIsProcessor || CurrentDef.bIsSafe) { ZOff = CurrentDef.bIsWallMount ? 0.f : CurrentDef.BoxHalf.Z; }
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

	// Plaatsing-cirkel niet meer nodig bij upgrades (ze snappen op het object + de doel-ring highlight 't al).
	if (RangeRing) { RangeRing->SetVisibility(false); }
	// Doel-object highlighten: het object (pot/rek/machine) waar deze upgrade op gaat snappen.
	if (TargetRing)
	{
		AActor* T = (bShow && CurUpgradeKind != 0) ? SnapTarget.Get() : nullptr;
		TargetRing->SetVisibility(T != nullptr);
		if (T) { TargetRing->SetWorldLocation(T->GetActorLocation() + FVector(0.f, 0.f, 5.f)); }
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
				UWeedToast::NotifyPawn(GetOwner(),-1, 2.5f, FColor::Orange, TEXT("Harvest the plant before picking up the pot."));
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
			if (GEngine) { UWeedToast::NotifyPawn(GetOwner(),-1, 2.5f, FColor::Orange, TEXT("Empty the drying rack before picking it up.")); }
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
			if (GEngine) { UWeedToast::NotifyPawn(GetOwner(),-1, 2.5f, FColor::Orange, TEXT("Empty the shelf before picking it up.")); }
			return;
		}
		ReturnItem = Shelf->ShelfTier;
	}
	else if (Cast<AWaterSink>(Target))
	{
		return; // gootsteen = vaste fixture, niet oppakbaar
	}
	else if (Cast<ACeilingLamp>(Target))
	{
		ReturnItem = FName(TEXT("Lamp_Ceiling"));
	}
	else if (Cast<AAtm>(Target))
	{
		ReturnItem = FName(TEXT("Atm"));
	}
	else if (Cast<APackLightSwitch>(Target))
	{
		ReturnItem = FName(TEXT("LightSwitch")); // schakelaar terug als inventory-item (was: viel in de else -> niets)
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
		UWeedToast::NotifyPawn(GetOwner(),-1, 2.f, FColor::Green, TEXT("Picked up."));
	}
}

void UBuildComponent::UpdateRemoteGhost()
{
	// Niet aan het plaatsen -> alle preview-objecten verbergen/opruimen.
	if (!bRepPlacing)
	{
		if (Ghost) { Ghost->SetVisibility(false); }
		if (PreviewActor.IsValid()) { DestroyPreview(); }
		RemotePreviewItem = NAME_None;
		return;
	}

	FPlaceableDef Def;
	if (!GetPlaceableDef(RepItemId, Def))
	{
		return;
	}

	// Toon het ECHTE model (zoals de lokale preview) i.p.v. een primitief blok. (Re)spawn als het
	// gekozen item wijzigt; daarna alleen verplaatsen.
	if (RemotePreviewItem != RepItemId || !PreviewActor.IsValid())
	{
		SpawnPreview(Def, RepItemId);
		RemotePreviewItem = RepItemId;
		if (Ghost) { Ghost->SetVisibility(false); }
	}

	// Zelfde Z-offset per type als de lokale preview (root op de vloer / lamp onder plafond / mid-pivot).
	float ZOff = 0.f;
	if (Def.bIsLamp) { ZOff = -Def.BoxHalf.Z; }
	else if (Def.bIsDryRack) { ZOff = Def.bIsWallMount ? 0.f : Def.BoxHalf.Z; }
	// Wand-mount (bv. het schap): RepLocation is al het midden -> geen extra Z-offset.
	else if (Def.bIsSink || Def.bIsPackBench || Def.bIsShelf || Def.bIsProcessor || Def.bIsSafe) { ZOff = Def.bIsWallMount ? 0.f : Def.BoxHalf.Z; }

	if (AActor* P = PreviewActor.Get())
	{
		P->SetActorHiddenInGame(false);
		P->SetActorLocationAndRotation(RepLocation + FVector(0.f, 0.f, ZOff), FRotator(0.f, RepYaw, 0.f));
		if (PreviewMID)
		{
			PreviewMID->SetVectorParameterValue(TEXT("GhostColor"),
				bRepValid ? FLinearColor(0.15f, 0.5f, 1.f, 1.f) : FLinearColor(1.f, 0.15f, 0.15f, 1.f));
		}
	}
}

bool UBuildComponent::IsPickable(const AActor* A) const
{
	if (!A || A->ActorHasTag(FName(TEXT("Cosmetic")))) { return false; } // NPC-woning-meubels: niet oppakbaar
	return (Cast<AGrowPlant>(A) || Cast<APlaceableProp>(A) || Cast<ADryingRack>(A)
		|| Cast<APackBench>(A) || Cast<AStorageShelf>(A) || Cast<ACeilingLamp>(A)
		|| Cast<AAtm>(A) || Cast<APackLightSwitch>(A));
}

void UBuildComponent::RefreshBuildArea()
{
	bHaveBuildArea = false;
	BuildAreaBox = FBox(ForceInit);
	UWorld* W = GetWorld();
	if (!W) { return; }
	const FString File = FPaths::ProjectSavedDir() / TEXT("BuildArea.txt");
	FString Content;
	if (!FFileHelper::LoadFileToString(Content, *File)) { return; }
	const FString MapPath = W->GetOutermost()->GetName();
	TArray<FString> Lines; Content.ParseIntoArrayLines(Lines);
	for (const FString& Ln : Lines)
	{
		TArray<FString> Parts; Ln.ParseIntoArray(Parts, TEXT("|"), true);
		if (Parts.Num() < 7 || Parts[0] != MapPath) { continue; }
		const FVector C1(FCString::Atof(*Parts[1]), FCString::Atof(*Parts[2]), FCString::Atof(*Parts[3]));
		const FVector C2(FCString::Atof(*Parts[4]), FCString::Atof(*Parts[5]), FCString::Atof(*Parts[6]));
		const float FloorZ = FMath::Min(C1.Z, C2.Z);
		BuildAreaBox = FBox(
			FVector(FMath::Min(C1.X, C2.X), FMath::Min(C1.Y, C2.Y), FloorZ - 70.f),
			FVector(FMath::Max(C1.X, C2.X), FMath::Max(C1.Y, C2.Y), FloorZ + 380.f));
		bHaveBuildArea = true; // laatste geldige regel voor deze map wint
	}
}

ADoorRetrofitter* UBuildComponent::GetCompRetro() const
{
	if (CompRetroCache.IsValid()) { return CompRetroCache.Get(); }
	const UWorld* W = GetWorld();
	if (!W) { return nullptr; }
	for (TActorIterator<ADoorRetrofitter> It(W); It; ++It) { CompRetroCache = *It; return *It; }
	return nullptr;
}

void UBuildComponent::RefreshNoBuildZones()
{
	NoBuildZones.Reset();
	UWorld* W = GetWorld();
	if (!W) { return; }
	// EIGEN bestand (niet MarkedSpots.txt - dat wordt door andere dev-tools leeggemaakt). Hierin worden de
	// no-build-markers vastgezet via WeedSaveNoBuild; zo verdwijnen je zones nooit meer.
	const FString File = FPaths::ProjectSavedDir() / TEXT("NoBuildZones.txt");
	FString Content;
	if (!FFileHelper::LoadFileToString(Content, *File)) { return; }
	const FString MapName = W->GetOutermost()->GetName();
	// Lees alle markers voor deze map (formaat: "F9 | map=<map> | pos=(X, Y, Z) | yaw=..").
	TArray<FVector> Pts;
	TArray<FString> Lines; Content.ParseIntoArrayLines(Lines);
	for (const FString& Ln : Lines)
	{
		if (!Ln.Contains(MapName)) { continue; }
		int32 PosStart = Ln.Find(TEXT("pos=("));
		if (PosStart == INDEX_NONE) { continue; }
		PosStart += 5;
		const int32 PosEnd = Ln.Find(TEXT(")"), ESearchCase::IgnoreCase, ESearchDir::FromStart, PosStart);
		if (PosEnd == INDEX_NONE) { continue; }
		TArray<FString> N;
		Ln.Mid(PosStart, PosEnd - PosStart).ParseIntoArray(N, TEXT(","), true);
		if (N.Num() < 3) { continue; }
		Pts.Add(FVector(FCString::Atof(*N[0].TrimStartAndEnd()), FCString::Atof(*N[1].TrimStartAndEnd()), FCString::Atof(*N[2].TrimStartAndEnd())));
	}
	// Elk PAAR markers = een no-build-box (min/max XY). Z loopt van net onder de vloer tot boven plafondhoogte
	// (markers staan op heuphoogte ~+88), zodat ook hoge wall-mounts (lightswitch/TV) op die muur geblokkeerd zijn.
	for (int32 i = 0; i + 1 < Pts.Num(); i += 2)
	{
		const FVector A = Pts[i], B = Pts[i + 1];
		const float MidZ = (A.Z + B.Z) * 0.5f;
		NoBuildZones.Add(FBox(
			FVector(FMath::Min(A.X, B.X), FMath::Min(A.Y, B.Y), MidZ - 200.f),
			FVector(FMath::Max(A.X, B.X), FMath::Max(A.Y, B.Y), MidZ + 360.f)));
	}

	// COMPETITIVE co-op: de retrofitter levert de naar 603/602 verschoven no-build-zones mee (leeg buiten competitive).
	if (ADoorRetrofitter* Retro = GetCompRetro())
	{
		TArray<FBox> CZ; Retro->GetCompetitiveNoBuildZones(CZ);
		NoBuildZones.Append(CZ);
	}
}

bool UBuildComponent::IsInNoBuildZone(const FVector& P) const
{
	for (const FBox& Z : NoBuildZones) { if (Z.IsInsideOrOn(P)) { return true; } }
	return false;
}

bool UBuildComponent::IsInOwnedHome(const FVector& P) const
{
	// COMPETITIVE co-op: de eigen gespiegelde kamer (Apt 603/602) is bouwbaar. Gecachete retrofitter (geen
	// actor-scan per call); leeg/geen effect buiten competitive. Alleen geraadpleegd op de pack-map / bij een
	// build-marker, dus de city-map betaalt hier niets voor.
	// Welke kamer valideren we? De ACTERENDE speler bepaalt dat, EXPLICIET via de owner-pawn - niet via de
	// netmode. In ServerPlace draait dit op de listen-server (NM_ListenServer) met GetOwner() = de acterende
	// pawn: de host-pawn is daar locally-controlled, de joiner-pawn niet. Zo valideert de server tegen de
	// JOINER-kamer voor de joiner (anders kreeg 'ie altijd de host-box en werd elke plaatsing geweigerd),
	// en blijft de client-preview (locally-controlled) tegen z'n eigen kamer valideren.
	const APawn* OwnerPawn = Cast<APawn>(GetOwner());
	const bool bJoiner = OwnerPawn && !OwnerPawn->IsLocallyControlled();
	auto InCompHome = [this, &P, bJoiner]() -> bool
	{
		ADoorRetrofitter* Retro = GetCompRetro();
		if (!Retro) { return false; }
		TArray<FBox> CHB; Retro->GetCompetitiveHomeBoxes(bJoiner, CHB);
		for (const FBox& B : CHB) { if (B.IsInsideOrOn(P)) { return true; } }
		return false;
	};

	// Speler-markers zijn LEIDEND: is er een build-box gemarkeerd (Ctrl+F9), dan mag je ALLEEN daarbinnen
	// bouwen - in competitive ook in je eigen gespiegelde kamer (de solo-marker dekt 603/602 niet).
	if (bHaveBuildArea) { return BuildAreaBox.IsInsideOrOn(P) || InCompHome(); }

	const UWorld* World = GetWorld();
	AActor* Owner = GetOwner();
	if (!Owner || !World) { return false; }

	// PACK-MAP (CityBeachStrip): de retrofitter levert een gemeten huis-box. Plaatsen mag
	// dan ALLEEN binnen je eigen huis.
	{
		// COMPETITIVE: eigen gespiegelde kamer (603/602) eerst -> daar mag altijd gebouwd worden.
		if (InCompHome()) { return true; }
		for (TActorIterator<ADoorRetrofitter> It(World); It; ++It)
		{
			FVector Min, Max;
			if (It->GetHomeBox(Min, Max))
			{
				if (P.X >= Min.X && P.X <= Max.X && P.Y >= Min.Y && P.Y <= Max.Y && P.Z >= Min.Z && P.Z <= Max.Z) { return true; }
				// De gemeten box neemt de DICHTSTBIJZIJNDE wand per richting en is daardoor soms te krap (anker
				// niet gecentreerd, nissen, lage static props). Sta daarom ook toe wat BINNEN is (onder een dak)
				// en ruim bij je thuis-plek ligt -> je kunt in je hele huis bouwen, maar niet buiten/op het balkon.
				const FVector HA2 = It->GetHomeAnchor();
				if (!HA2.IsNearlyZero()
					&& FMath::Abs(P.X - HA2.X) <= 850.f && FMath::Abs(P.Y - HA2.Y) <= 850.f
					&& P.Z >= HA2.Z - 220.f && P.Z <= HA2.Z + 520.f && IsIndoors(P)) { return true; }
				return false;
			}
			// Wand-box nog niet (betrouwbaar) gemeten -> ruime fallback rond de thuis-plek, zodat je
			// ALTIJD in je eigen huis kunt bouwen (anders kan een Normal-speler niets plaatsen).
			const FVector HA = It->GetHomeAnchor();
			if (!HA.IsNearlyZero())
			{
				const bool bInBox = FMath::Abs(P.X - HA.X) <= 750.f && FMath::Abs(P.Y - HA.Y) <= 750.f
					&& P.Z >= HA.Z - 220.f && P.Z <= HA.Z + 520.f;
				return bInBox && IsIndoors(P); // binnen + onder een dak (geen balkon/buiten)
			}
			break; // geen thuis-plek bekend -> val door naar de free-build fallback
		}
	}

	// Sandbox/free-build zonder eigen woning: val terug op "overal binnen toegestaan, niet buiten".
	if (WorldFreeBuild(World)) { return IsIndoors(P); }
	return false;
}

bool UBuildComponent::IsIndoors(const FVector& FloorPoint) const
{
	if (!bIndoorsOnly) { return true; }
	const UWorld* World = GetWorld();
	if (!World) { return true; }

	// Trace recht omhoog: raken we een plafond/dak -> binnen. Buiten is er open lucht boven.
	// We checken NIET alleen het midden maar ook een paar punten eromheen: staat de plek net onder een
	// deuropening of trapgat, dan vangt een naburig punt het plafond alsnog -> geen valse "outside".
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
	TArray<FOverlapResult> ClipHits;
	if (World->OverlapMultiByObjectType(ClipHits, Center, Rot, ObjParams, ClipBox, QP))
	{
		for (const FOverlapResult& H : ClipHits)
		{
			// DEUR: negeer ALLEEN de nabijheid-trigger (USphereComponent, ~150cm auto-open-zone) - die telde mee als
			// obstakel en blokkeerde een groot rond vlak "waar de deur niet eens komt". Het deurBLAD (static mesh)
			// telt WEL, zodat je niet middenin de deur plaatst. Muren/meubels tellen altijd.
			const AActor* HA = H.GetActor();
			if (HA && HA->IsA(ACityDoor::StaticClass()) && !Cast<UStaticMeshComponent>(H.GetComponent())) { continue; }
			return true; // echt obstakel (muur/meubel/deurblad)
		}
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

	// Upgrade? Dan moet 'ie bij een geldig object (pot/rek/machine) staan (mirror van de snap). De vloer-/
	// blokkade-/woning-checks slaan we over (de upgrade snapt bewust tegen het object aan).
	const FString IdS = ItemId.ToString();
	const int32 UpKind = (GearUpgradeIndex(ItemId) >= 0) ? 1 : (IdS.StartsWith(TEXT("DryUp_")) ? 2 : (IdS.StartsWith(TEXT("ProcUp_")) ? 3 : 0));
	const bool bUpgrade = (UpKind != 0);
	AActor* UpgTarget = bUpgrade ? FindUpgradeTarget(UpKind, Location) : nullptr;
	if (bUpgrade && !UpgTarget)
	{
		if (GEngine) { UWeedToast::NotifyPawn(GetOwner(), -1, 2.f, FColor::Orange, TEXT("Place this on a pot/rack/machine.")); }
		return;
	}
	// Pot-gear: max 1 per upgrade EN max 1 tier per familie (lamp/tent/water) per pot.
	if (bUpgrade && UpKind == 1)
	{
		if (AGrowPlant* TargetPot = Cast<AGrowPlant>(UpgTarget))
		{
			const int32 NewUi = GearUpgradeIndex(ItemId);
			// Exact dezelfde upgrade al aanwezig -> weigeren.
			if (NewUi >= 0 && TargetPot->HasPotUpgrade(NewUi))
			{
				if (GEngine) { UWeedToast::NotifyPawn(GetOwner(), -1, 2.5f, FColor::Orange, TEXT("This pot already has that upgrade.")); }
				return;
			}
			// Tier-familie: hogere tier plaatsen haalt de lagere prop weg + terug naar inventory (geen stacking);
			// een lagere tier op een hogere is een downgrade -> weigeren.
			if (NewUi >= 0)
			{
				const TArray<int32> Fam = GearFamilyIndices(NewUi);
				if (Fam.Num() > 1)
				{
					for (int32 OtherUi : Fam)
					{
						if (OtherUi == NewUi || !TargetPot->HasPotUpgrade(OtherUi)) { continue; }
						if (OtherUi > NewUi)
						{
							if (GEngine) { UWeedToast::NotifyPawn(GetOwner(), -1, 2.5f, FColor::Orange, TEXT("This pot already has a higher-tier upgrade.")); }
							return;
						}
						const FName OldGearId = GearItemForUpgrade(OtherUi);
						const FVector C = TargetPot->GetActorLocation();
						for (TActorIterator<APlaceableProp> It(World); It; ++It)
						{
							APlaceableProp* P = *It;
							if (!IsValid(P) || P->ItemId != OldGearId) { continue; }
							const FVector L = P->GetActorLocation();
							if (FVector::Dist2D(L, C) > 175.f || FMath::Abs(L.Z - C.Z) > 280.f) { continue; }
							if (UInventoryComponent* Inv = GetOwnerInventory()) { Inv->AddItem(OldGearId, 1); }
							P->Destroy();
							break;
						}
					}
				}
			}
		}
	}

	// Gootsteen = vaste fixture in een normaal spel (niet plaatsbaar). In SANDBOX/free-build mag 't wél,
	// zodat je de sink-positie voor de template kunt inrichten.
	if (ItemId == FName(TEXT("Sink")) && !WorldFreeBuild(World))
	{
		if (GEngine) { UWeedToast::NotifyPawn(GetOwner(), -1, 2.f, FColor::Orange, TEXT("Sinks are fixed fixtures.")); }
		return;
	}

	// Server-side her-validatie (anti-cheat / lag): niet in een muur of (voor potten) te dicht.
	// Plafondlampen (plafond) en wand-mounts (muur) gebruiken geen vloer-gebaseerde checks -> overslaan.
	if (!bUpgrade && !Def.bIsLamp && !Def.bIsWallMount && IsSpotBlocked(Location, Def.BoxHalf, Rotation.Yaw, Def.bIsPot))
	{
		if (GEngine)
		{
			UWeedToast::NotifyPawn(GetOwner(),-1, 2.f, FColor::Red, TEXT("Can't place there (blocked)."));
		}
		return;
	}
	// Server-parity met de preview-ghost (r~889): weiger ook in een expliciete no-build-zone EN in de deur-
	// footprint-zone (drempel/zwaai vrijhouden, met de object-footprint mee). Deur-cache eerst vullen (server heeft
	// z'n eigen cache, gevuld rond de plaats-locatie). Zelfde gate als de preview: geen upgrade/lamp/wand-mount.
	if (!bUpgrade && !Def.bIsLamp && !Def.bIsWallMount)
	{
		RefreshDoorCache(Location);
		if (IsInNoBuildZone(Location) || DoorBlocksCell(Location, Def.BoxHalf, Rotation.Yaw))
		{
			if (GEngine) { UWeedToast::NotifyPawn(GetOwner(), -1, 2.f, FColor::Red, TEXT("Keep the doorway clear.")); }
			return;
		}
	}
	// Niet bovenop een ander geplaatst object (stapelen): korte omlaag-trace; raakt 'ie een placeable i.p.v.
	// de vloer, dan weigeren. Wand-mounts (muur) en plafondlampen (plafond) slaan dit over.
	if (!bUpgrade && !Def.bIsLamp && !Def.bIsWallMount)
	{
		FHitResult Down;
		FCollisionQueryParams DQP(SCENE_QUERY_STAT(WeedShopStackCheck), false);
		DQP.AddIgnoredActor(GetOwner());
		if (World->LineTraceSingleByChannel(Down, Location + FVector(0.f, 0.f, 20.f), Location - FVector(0.f, 0.f, 20.f), ECC_Visibility, DQP)
			&& IsPlaceableActor(Down.GetActor()))
		{
			if (GEngine) { UWeedToast::NotifyPawn(GetOwner(), -1, 2.f, FColor::Red, TEXT("Can't stack on another object.")); }
			return;
		}
	}
	// Wand-mounts/plafondlampen: niet DOOR een ander geplaatst object heen (zijdelingse overlap/clippen).
	if (!bUpgrade && (Def.bIsLamp || Def.bIsWallMount)
		&& !Def.bIsStructure
		&& OverlapsOtherPlaceable(World, GetOwner(), Location, Def.BoxHalf, Rotation.Quaternion()))
	{
		if (GEngine) { UWeedToast::NotifyPawn(GetOwner(), -1, 2.f, FColor::Red, TEXT("Can't place inside another object.")); }
		return;
	}
	// Wand-mount: niet met de voorkant DOOR een haakse / hoek-muur (WorldStatic) steken. Server-parity met de preview (r~725).
	if (!bUpgrade && Def.bIsWallMount && !Def.bIsStructure
		&& WallItemClipsWall(World, GetOwner(), Location, Def.BoxHalf, Rotation.Quaternion(), Def.bIsLightSwitch))
	{
		if (GEngine) { UWeedToast::NotifyPawn(GetOwner(), -1, 2.f, FColor::Red, TEXT("Too close to a corner or wall.")); }
		return;
	}

	// Alleen BINNEN (je eigen woning; in free-build overal binnen) - NOOIT buiten, tenzij outdoors-toegestaan.
	// (IsInOwnedHome regelt zelf de free-build-uitzondering via een binnen-check.)
	if (Def.bIsStructure)
	{
		// Building-tool: geen overlap-/binnen-regels (vrij tekenen van muren/vloeren).
	}
	else if (!bUpgrade && !Def.bAllowOutdoors)
	{
		bool bHomeOk = IsInOwnedHome(Location);
		// Wand-mount staat op de muur (= op de build-box-rand), dus Location valt soms nét buiten. Check dan ook
		// een punt iets de kamer in (4 richtingen) - net als de preview - zodat plaatsen op je EIGEN muur lukt.
		// Buiten je huis blijft 't geweigerd (geen enkel richtingspunt valt dan binnen).
		if (!bHomeOk && Def.bIsWallMount)
		{
			bHomeOk = IsInOwnedHome(Location + FVector(45.f, 0.f, 0.f)) || IsInOwnedHome(Location + FVector(-45.f, 0.f, 0.f))
				|| IsInOwnedHome(Location + FVector(0.f, 45.f, 0.f)) || IsInOwnedHome(Location + FVector(0.f, -45.f, 0.f));
		}
		if (!bHomeOk)
		{
			if (GEngine)
			{
				UWeedToast::NotifyPawn(GetOwner(),-1, 2.5f, FColor::Orange, TEXT("You can only build inside (not outdoors)."));
			}
			return;
		}
	}

	UInventoryComponent* Inv = GetOwnerInventory();
	// Bouw-onderdelen (building-tool) zijn ONEINDIG: niet verbruiken bij het plaatsen.
	if (!Def.bIsStructure)
	{
		if (!Inv || !Inv->RemoveItem(ItemId, 1))
		{
			return;
		}
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = GetOwner();
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	if (Def.bIsAtm)
	{
		// Geldautomaat: spawn een interactieve AAtm.
		World->SpawnActor<AAtm>(AAtm::StaticClass(), FTransform(Rotation, Location), SpawnParams);
	}
	else if (Def.bIsSafe)
	{
		// Kluis = opslag-kist (AStorageShelf-tier): items erin slepen i.p.v. cash. ShelfTier voor FinishSpawning
		// zetten zodat BeginPlay het als safe herkent (geen bederf-timer).
		if (AStorageShelf* S = World->SpawnActorDeferred<AStorageShelf>(AStorageShelf::StaticClass(), FTransform(Rotation, Location), GetOwner(), nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn))
		{
			S->ShelfTier = Def.ItemId;
			S->FinishSpawning(FTransform(Rotation, Location));
		}
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
		// Opslag-schap: wand-mount (schap) -> Location is al het midden (preview rekent de muur-offset uit).
		// Vloer-variant (kist) -> mesh-pivot in het midden, origin een halve hoogte omhoog.
		const FTransform ShelfTM(Rotation, Def.bIsWallMount ? Location : Location + FVector(0.f, 0.f, Def.BoxHalf.Z));
		AStorageShelf* Shelf = World->SpawnActorDeferred<AStorageShelf>(
			AStorageShelf::StaticClass(), ShelfTM,
			GetOwner(), nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (Shelf)
		{
			Shelf->ShelfTier = ItemId;
			Shelf->FinishSpawning(ShelfTM);
		}
	}
	else if (Def.bIsProcessor)
	{
		// Hasj-machine (mesh/press): mesh-pivot in het midden -> origin een halve hoogte omhoog.
		const FTransform ProcTM(Rotation, Location + FVector(0.f, 0.f, Def.BoxHalf.Z));
		AProcessorMachine* Proc = World->SpawnActorDeferred<AProcessorMachine>(
			AProcessorMachine::StaticClass(), ProcTM,
			GetOwner(), nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (Proc)
		{
			Proc->MachineTier = ItemId;
			Proc->FinishSpawning(ProcTM);
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
	else if (Def.bIsLightSwitch)
	{
		APackLightSwitch* Sw = World->SpawnActorDeferred<APackLightSwitch>(
			APackLightSwitch::StaticClass(), FTransform(Rotation, Location),
			GetOwner(), nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (Sw)
		{
			// Stabiele sleutel uit de wereldpositie (op 10cm) -> aan/uit + dim onthouden per plek/woning.
			Sw->Setup(FString::Printf(TEXT("sw_%d_%d_%d"),
				FMath::RoundToInt(Location.X / 10.f), FMath::RoundToInt(Location.Y / 10.f), FMath::RoundToInt(Location.Z / 10.f)), 800.f);
			Sw->FinishSpawning(FTransform(Rotation, Location));
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
		UWeedToast::NotifyPawn(GetOwner(),-1, 2.f, FColor::Green, TEXT("Placed."));
	}
}
