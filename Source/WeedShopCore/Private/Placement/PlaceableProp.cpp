#include "Placement/PlaceableProp.h"

#include "Placement/PlaceableTypes.h"
#include "Placement/PropMeshKit.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Engine/StaticMesh.h"
#include "Net/UnrealNetwork.h"
#include "Game/WeedShopGameState.h"
#include "World/DayCycleComponent.h"
#include "Save/SaveGameSubsystem.h"
#include "Phone/PhoneClientComponent.h"
#include "UI/WeedToast.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Interaction/Interactable.h"
#include "Cultivation/PotTypes.h" // GearUpgradeIndex -> IsGearUpgrade()

// Statische registry van alle levende props (zie GetAll in de header): gevuld in BeginPlay,
// geleegd in EndPlay. De gear-scans (pot/rek/machine) lopen hierdoor O(props) i.p.v. over alle actors.
static TArray<TWeakObjectPtr<APlaceableProp>> GPlaceablePropRegistry;
const TArray<TWeakObjectPtr<APlaceableProp>>& APlaceableProp::GetAll() { return GPlaceablePropRegistry; }

APlaceableProp::APlaceableProp()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
	Root->SetMobility(EComponentMobility::Movable);

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Root);
	// WorldStatic + query/physics zodat de plaats-trace en footprint-overlap 'm zien.
	Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Mesh->SetMobility(EComponentMobility::Movable);

	Deco = PropKit::MakeDeco(this, Root, TEXT("Deco"));
	Deco->SetMobility(EComponentMobility::Movable);
	for (int32 i = 0; i < 8; ++i)
	{
		UStaticMeshComponent* P = PropKit::MakePart(this, Deco, *FString::Printf(TEXT("Part%d"), i));
		if (P) { P->SetMobility(EComponentMobility::Movable); }
		Parts.Add(P);
	}

	RuntimeModel = CreateDefaultSubobject<USceneComponent>(TEXT("RuntimeModel"));
	RuntimeModel->SetupAttachment(Root);
	RuntimeModel->SetUsingAbsoluteScale(true); // kinderen in echte cm, negeer root-schaal
}

void APlaceableProp::HideParts()
{
	for (UStaticMeshComponent* P : Parts) { if (P) { P->SetVisibility(false); } }
}

void APlaceableProp::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(APlaceableProp, ItemId);
	DOREPLIFETIME(APlaceableProp, bDoorOpen);
}

void APlaceableProp::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	SetupVisual();
}

void APlaceableProp::BeginPlay()
{
	Super::BeginPlay();
	GPlaceablePropRegistry.Add(this);
	SetupVisual();
}

void APlaceableProp::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GPlaceablePropRegistry.Remove(this);
	Super::EndPlay(EndPlayReason);
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
	// Mesh omhoog zodat de onderkant op de actor-origin (= vloer) staat. Pack-meshes met een pivot AAN
	// DE BASIS (bBasePivot) staan al goed -> niet extra omhoog schuiven (anders zweven ze).
	Mesh->SetRelativeLocation(FVector(0.f, 0.f, Def.bBasePivot ? 0.f : Def.BoxHalf.Z));

	// Bouw-onderdelen (building-tool): pack-meshes hebben hoek/eind-pivots -> centreer op de
	// prop-origin. Muren zijn enkelzijdig: spiegel-mesh erbij zodat ze van beide kanten zichtbaar zijn.
	if (Def.bIsStructure)
	{
		const FString SId = ItemId.ToString();
		if (Def.bIsHingedDoor)
		{
			// Deurblad: pivot = het scharnier (mesh-eigenschap) -> relatief op de origin laten staan,
			// zodat SetRelativeRotation netjes om het scharnier draait.
			Mesh->SetRelativeLocation(FVector::ZeroVector);
			Mesh->SetRelativeRotation(FRotator(0.f, bDoorOpen ? -100.f : 0.f, 0.f));
			HideParts();
			return;
		}
		if (SId == TEXT("Struct_CeilLamp"))
		{
			Mesh->SetRelativeLocation(FVector::ZeroVector); // pivot zit al aan de plafond-kant
			if (!StructLight)
			{
				StructLight = NewObject<UPointLightComponent>(this);
				StructLight->SetupAttachment(GetRootComponent());
				StructLight->RegisterComponent();
				StructLight->SetMobility(EComponentMobility::Movable);
				StructLight->SetRelativeLocation(FVector(0.f, 0.f, -28.f));
				StructLight->SetIntensity(3200.f);
				StructLight->SetLightColor(FLinearColor(1.f, 0.93f, 0.82f)); // warm wit
				StructLight->SetAttenuationRadius(950.f);
				StructLight->SetCastShadows(false);
			}
			HideParts();
			return;
		}
		if (SId.StartsWith(TEXT("Struct_Wall")))
		{
			Mesh->SetRelativeLocation(FVector(2.5f, Def.BoxHalf.Y, 0.f)); // eind-pivot, span lokaal -Y
			if (Def.bIsDoubleWall)
			{
				if (!MirrorMesh)
				{
					MirrorMesh = NewObject<UStaticMeshComponent>(this);
					MirrorMesh->SetupAttachment(GetRootComponent());
					MirrorMesh->RegisterComponent();
					MirrorMesh->SetMobility(EComponentMobility::Movable);
					MirrorMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision); // collision zit al op de hoofd-mesh
					MirrorMesh->SetCanEverAffectNavigation(false);
				}
				MirrorMesh->SetStaticMesh(Mesh->GetStaticMesh());
				MirrorMesh->SetRelativeLocation(FVector(-2.5f, -Def.BoxHalf.Y, 0.f));
				MirrorMesh->SetRelativeRotation(FRotator(0.f, 180.f, 0.f));
			}
		}
		else
		{
			Mesh->SetRelativeLocation(FVector(Def.BoxHalf.X, Def.BoxHalf.Y, 0.f)); // hoek-pivot (max-hoek)
		}
		HideParts();
		return; // geen mockup-onderdelen
	}

	// Samengestelde mockups voor de meest "blok"-achtige meubels. Vloer = z=0; object groeit omhoog.
	const float W = Def.MeshScale.X * 100.f;
	const float D = Def.MeshScale.Y * 100.f;
	const float H = Def.MeshScale.Z * 100.f;
	const FString Id = ItemId.ToString();
	if (Parts.Num() < 8) { return; }

	if (Id == TEXT("Table"))
	{
		Mesh->SetVisibility(false); HideParts();
		const FLinearColor Wood(0.45f, 0.31f, 0.18f), Leg(0.30f, 0.21f, 0.12f);
		const float TopT = FMath::Min(7.f, H * 0.12f);
		const float LegW = FMath::Min(7.f, W * 0.09f);
		const float PX = W * 0.5f - LegW * 0.7f, PY = D * 0.5f - LegW * 0.7f;
		const float LegH = H - TopT;
		PropKit::SetPart(Parts[0], PropKit::Cube(), FVector(W, D, TopT), FVector(0, 0, H - TopT * 0.5f), Wood);
		PropKit::SetPart(Parts[1], PropKit::Cube(), FVector(LegW, LegW, LegH), FVector( PX,  PY, LegH * 0.5f), Leg);
		PropKit::SetPart(Parts[2], PropKit::Cube(), FVector(LegW, LegW, LegH), FVector( PX, -PY, LegH * 0.5f), Leg);
		PropKit::SetPart(Parts[3], PropKit::Cube(), FVector(LegW, LegW, LegH), FVector(-PX,  PY, LegH * 0.5f), Leg);
		PropKit::SetPart(Parts[4], PropKit::Cube(), FVector(LegW, LegW, LegH), FVector(-PX, -PY, LegH * 0.5f), Leg);
	}
	else if (Id == TEXT("Fridge"))
	{
		Mesh->SetVisibility(false); HideParts();
		const FLinearColor Body(0.80f, 0.82f, 0.85f), Seam(0.55f, 0.57f, 0.60f), Handle(0.25f, 0.26f, 0.28f);
		PropKit::SetPart(Parts[0], PropKit::Cube(), FVector(W, D, H), FVector(0, 0, H * 0.5f), Body);
		// Deurnaad (vriesvak boven).
		PropKit::SetPart(Parts[1], PropKit::Cube(), FVector(W * 1.01f, D * 0.04f, 2.f), FVector(0, D * 0.5f, H * 0.66f), Seam);
		// 2 handvatten op de deur (voorzijde +Y).
		PropKit::SetPart(Parts[2], PropKit::Cube(), FVector(4.f, 5.f, H * 0.20f), FVector(W * 0.32f, D * 0.52f, H * 0.80f), Handle);
		PropKit::SetPart(Parts[3], PropKit::Cube(), FVector(4.f, 5.f, H * 0.34f), FVector(W * 0.32f, D * 0.52f, H * 0.40f), Handle);
		Parts[4]->SetVisibility(false);
	}
	else if (Id == TEXT("Wardrobe"))
	{
		Mesh->SetVisibility(false); HideParts();
		const FLinearColor Wood(0.40f, 0.28f, 0.16f), DoorC(0.50f, 0.36f, 0.22f), Trim(0.20f, 0.13f, 0.08f), Handle(0.88f, 0.82f, 0.55f);
		PropKit::SetPart(Parts[0], PropKit::Cube(), FVector(W, D, H), FVector(0, 0, H * 0.5f), Wood);                                    // romp
		PropKit::SetPart(Parts[1], PropKit::Cube(), FVector(W * 0.47f, D * 0.06f, H * 0.88f), FVector(-W * 0.25f, D * 0.50f, H * 0.52f), DoorC); // linkerdeur
		PropKit::SetPart(Parts[2], PropKit::Cube(), FVector(W * 0.47f, D * 0.06f, H * 0.88f), FVector( W * 0.25f, D * 0.50f, H * 0.52f), DoorC); // rechterdeur
		PropKit::SetPart(Parts[3], PropKit::Cube(), FVector(W * 0.02f, D * 0.07f, H * 0.88f), FVector(0, D * 0.50f, H * 0.52f), Trim);   // middennaad
		PropKit::SetPart(Parts[4], PropKit::Cube(), FVector(3.f, 4.f, H * 0.16f), FVector(-W * 0.06f, D * 0.55f, H * 0.55f), Handle);    // greep links
		PropKit::SetPart(Parts[5], PropKit::Cube(), FVector(3.f, 4.f, H * 0.16f), FVector( W * 0.06f, D * 0.55f, H * 0.55f), Handle);    // greep rechts
		PropKit::SetPart(Parts[6], PropKit::Cube(), FVector(W * 1.05f, D * 1.05f, H * 0.04f), FVector(0, 0, H * 0.02f), Trim);           // plint
		PropKit::SetPart(Parts[7], PropKit::Cube(), FVector(W * 1.06f, D * 1.06f, H * 0.03f), FVector(0, 0, H * 0.985f), Trim);          // kroonlijst
	}
	else if (Id == TEXT("Mattress"))
	{
		Mesh->SetVisibility(false); HideParts();
		const FLinearColor Base(0.20f, 0.22f, 0.28f), Pad(0.78f, 0.78f, 0.82f), Pillow(0.9f, 0.55f, 0.45f);
		PropKit::SetPart(Parts[0], PropKit::Cube(), FVector(W, D, H * 0.45f), FVector(0, 0, H * 0.225f), Base);
		PropKit::SetPart(Parts[1], PropKit::Cube(), FVector(W * 0.98f, D * 0.98f, H * 0.55f), FVector(0, 0, H * 0.72f), Pad);
		PropKit::SetPart(Parts[2], PropKit::Cube(), FVector(W * 0.34f, D * 0.5f, H * 0.30f), FVector(-W * 0.28f, 0, H * 1.05f), Pillow);
		for (int32 i = 3; i < 8; ++i) { Parts[i]->SetVisibility(false); }
	}
	else if (Id.StartsWith(TEXT("Gear_Lamp")))
	{
		// Kweeklamp: voet + steel + lampkop met gloei-paneel.
		Mesh->SetVisibility(false); HideParts();
		const FLinearColor Pole(0.25f, 0.26f, 0.30f), Head(0.18f, 0.19f, 0.22f), Glow(1.f, 0.95f, 0.6f);
		PropKit::SetPart(Parts[0], PropKit::Cube(), FVector(W * 0.55f, D * 0.55f, H * 0.06f), FVector(0, 0, H * 0.03f), Pole);          // voet
		PropKit::SetPart(Parts[1], PropKit::Cube(), FVector(W * 0.16f, D * 0.16f, H * 0.78f), FVector(0, 0, H * 0.42f), Pole);          // steel
		PropKit::SetPart(Parts[2], PropKit::Cube(), FVector(W, D * 0.7f, H * 0.14f),          FVector(0, 0, H * 0.88f), Head);          // lampkop
		PropKit::SetPart(Parts[3], PropKit::Cube(), FVector(W * 0.86f, D * 0.55f, H * 0.05f), FVector(0, 0, H * 0.79f), Glow);          // gloei-paneel
		for (int32 i = 4; i < 8; ++i) { Parts[i]->SetVisibility(false); }
	}
	else if (Id.StartsWith(TEXT("Gear_Tent")))
	{
		// Kweektent: 4 stijlen + bovenframe + achterdoek.
		Mesh->SetVisibility(false); HideParts();
		const FLinearColor Frame(0.16f, 0.16f, 0.18f), Fabric(0.10f, 0.12f, 0.16f);
		const float PT = FMath::Max(2.5f, W * 0.07f);
		const float PX = W * 0.5f - PT * 0.5f, PY = D * 0.5f - PT * 0.5f;
		PropKit::SetPart(Parts[0], PropKit::Cube(), FVector(PT, PT, H), FVector( PX,  PY, H * 0.5f), Frame);
		PropKit::SetPart(Parts[1], PropKit::Cube(), FVector(PT, PT, H), FVector( PX, -PY, H * 0.5f), Frame);
		PropKit::SetPart(Parts[2], PropKit::Cube(), FVector(PT, PT, H), FVector(-PX,  PY, H * 0.5f), Frame);
		PropKit::SetPart(Parts[3], PropKit::Cube(), FVector(PT, PT, H), FVector(-PX, -PY, H * 0.5f), Frame);
		PropKit::SetPart(Parts[4], PropKit::Cube(), FVector(W, D, PT),  FVector(0, 0, H - PT * 0.5f), Frame);                 // top
		PropKit::SetPart(Parts[5], PropKit::Cube(), FVector(W * 0.96f, PT, H * 0.92f), FVector(0, -PY, H * 0.5f), Fabric);    // achterdoek
		for (int32 i = 6; i < 8; ++i) { Parts[i]->SetVisibility(false); }
	}
	else if (Id.StartsWith(TEXT("Gear_Water")))
	{
		// Watertank: ronde tank + dop + slangetje.
		Mesh->SetVisibility(false); HideParts();
		const FLinearColor Tank(0.28f, 0.45f, 0.66f), Cap(0.18f, 0.20f, 0.24f), Tube(0.55f, 0.6f, 0.66f);
		PropKit::SetPart(Parts[0], PropKit::Cylinder(), FVector(W * 0.85f, D * 0.85f, H * 0.82f), FVector(0, 0, H * 0.41f), Tank);
		PropKit::SetPart(Parts[1], PropKit::Cylinder(), FVector(W * 0.42f, D * 0.42f, H * 0.12f), FVector(0, 0, H * 0.88f), Cap);
		PropKit::SetPart(Parts[2], PropKit::Cylinder(), FVector(2.5f, 2.5f, H * 0.5f), FVector(W * 0.45f, 0, H * 0.22f), Tube, FRotator(0.f, 0.f, 90.f));
		for (int32 i = 3; i < 8; ++i) { Parts[i]->SetVisibility(false); }
	}
	else if (Id == TEXT("Gear_Drainage"))
	{
		// Drainage: lage bak met grind.
		Mesh->SetVisibility(false); HideParts();
		const FLinearColor Tray(0.22f, 0.20f, 0.18f), Gravel(0.46f, 0.43f, 0.38f);
		PropKit::SetPart(Parts[0], PropKit::Cube(), FVector(W, D, H * 0.5f), FVector(0, 0, H * 0.25f), Tray);
		PropKit::SetPart(Parts[1], PropKit::Cube(), FVector(W * 0.82f, D * 0.82f, H * 0.45f), FVector(0, 0, H * 0.62f), Gravel);
		for (int32 i = 2; i < 8; ++i) { Parts[i]->SetVisibility(false); }
	}
	else if (Id == TEXT("Gear_Insulation"))
	{
		// Isolatie: ingepakt blok met een band.
		Mesh->SetVisibility(false); HideParts();
		const FLinearColor Wrap(0.75f, 0.72f, 0.55f), Band(0.55f, 0.40f, 0.20f);
		PropKit::SetPart(Parts[0], PropKit::Cube(), FVector(W, D, H), FVector(0, 0, H * 0.5f), Wrap);
		PropKit::SetPart(Parts[1], PropKit::Cube(), FVector(W * 1.03f, D * 0.26f, H * 0.20f), FVector(0, 0, H * 0.5f), Band);
		for (int32 i = 2; i < 8; ++i) { Parts[i]->SetVisibility(false); }
	}
	else if (Id == TEXT("Gear_Bloom"))
	{
		// Bloom booster: voedings-fles + dop + label.
		Mesh->SetVisibility(false); HideParts();
		const FLinearColor Bottle(0.28f, 0.55f, 0.30f), Cap(0.85f, 0.80f, 0.20f), Label(0.95f, 0.95f, 0.92f);
		PropKit::SetPart(Parts[0], PropKit::Cylinder(), FVector(W * 0.8f, D * 0.8f, H * 0.76f), FVector(0, 0, H * 0.38f), Bottle);
		PropKit::SetPart(Parts[1], PropKit::Cylinder(), FVector(W * 0.4f, D * 0.4f, H * 0.18f), FVector(0, 0, H * 0.87f), Cap);
		PropKit::SetPart(Parts[2], PropKit::Cube(), FVector(W * 0.7f, D * 0.06f, H * 0.32f), FVector(0, D * 0.4f, H * 0.4f), Label);
		for (int32 i = 3; i < 8; ++i) { Parts[i]->SetVisibility(false); }
	}
	else
	{
		// Basis-vorm placeholder (Cube/Cylinder/Cone uit /Engine/BasicShapes/) -> bouw hetzelfde herkenbare
		// primitieven-model dat het gedropte/hand-item ook gebruikt, i.p.v. een kale doos.
		const bool bBasicShape = Def.MeshPath && FString(Def.MeshPath).Contains(TEXT("/Engine/BasicShapes/"));
		if (bBasicShape && RuntimeModel)
		{
			Mesh->SetVisibility(false);            // root-doos verbergen (collision blijft via de Mesh-comp)
			HideParts();
			PropKit::ClearItemModel(RuntimeModel); // alleen onze eigen runtime-parts wissen (nooit Parts[])
			PropKit::BuildItemModel(this, RuntimeModel, ItemId, 1.f, /*bFirstPerson*/ false, /*bCollision*/ false);
		}
		else
		{
			Mesh->SetVisibility(true);
			HideParts();
			if (RuntimeModel) { PropKit::ClearItemModel(RuntimeModel); }
		}
	}
}

// Gear-upgrade-herkenning: pot-gear (GearUpgradeIndex), droogrek- (DryUp_) en machine- (ProcUp_) upgrades.
// Deze props zijn bewust NIET interactable: geen prompt-/interact-forward meer naar de host (de pot/het
// rek zelf krijgt de focus doordat de interactie-trace gear-props overslaat, zie UInteractionComponent).
bool APlaceableProp::IsGearUpgrade() const
{
	const FString S = ItemId.ToString();
	return GearUpgradeIndex(ItemId) >= 0 || S.StartsWith(TEXT("DryUp_")) || S.StartsWith(TEXT("ProcUp_"));
}

FText APlaceableProp::GetInteractionPrompt_Implementation() const
{
	FPlaceableDef Def;
	const bool bHas = GetPlaceableDef(ItemId, Def);
	if (bHas && Def.bIsBed)
	{
		return FText::FromString(TEXT("Sleep (skip to morning)  -  hold G to pick up"));
	}
	if (bHas && Def.bIsWardrobe)
	{
		return FText::FromString(TEXT("Wardrobe - change your outfit  -  hold G to pick up"));
	}
	if (bHas && Def.bIsHingedDoor)
	{
		return FText::FromString(bDoorOpen ? TEXT("Close door  -  hold G to pick up") : TEXT("Open door  -  hold G to pick up"));
	}
	const FString Name = bHas ? Def.DisplayName : ItemId.ToString();
	return FText::FromString(FString::Printf(TEXT("%s - hold G to pick up"), *Name));
}

void APlaceableProp::OnRep_DoorOpen()
{
	if (Mesh) { Mesh->SetRelativeRotation(FRotator(0.f, bDoorOpen ? -100.f : 0.f, 0.f)); }
}

void APlaceableProp::Interact_Implementation(APawn* InstigatorPawn)
{
	// Scharnier-deur (building-tool): F = open/dicht.
	{
		FPlaceableDef DoorDef;
		if (GetPlaceableDef(ItemId, DoorDef) && DoorDef.bIsHingedDoor)
		{
			if (HasAuthority())
			{
				bDoorOpen = !bDoorOpen;
				OnRep_DoorOpen();
			}
			return;
		}
	}

	// Alleen bedden doen iets: nacht overslaan tot 07:00 + hier opslaan (= je laad-/spawnpunt).
	FPlaceableDef Def;
	if (!GetPlaceableDef(ItemId, Def) || !Def.bIsBed || !HasAuthority()) { return; }

	AWeedShopGameState* GS = GetWorld() ? GetWorld()->GetGameState<AWeedShopGameState>() : nullptr;
	if (UDayCycleComponent* DC = GS ? GS->GetDayCycle() : nullptr)
	{
		if (DC->IsNight())
		{
			// 07:00 op de dag-klok -> bijbehorende seconden in de lichtfase.
			const float DayHours = FMath::Max(1.f, DC->SunsetHour - DC->SunriseHour);
			const float Morning = ((7.f - DC->SunriseHour) / DayHours) * DC->DayLengthSeconds;
			DC->SetTimeOfDaySeconds(Morning);
		}
	}

	// "Ik woon hier": slaap je in een bed in een ander (gekocht) apartment, dan verschuift je woon-/spawn-plek
	// automatisch hierheen (en dus ook waar een overval toeslaat). Vóór de save zodat 't meteen meegaat.
	if (InstigatorPawn)
	{
		if (UPhoneClientComponent* Ph = InstigatorPawn->FindComponentByClass<UPhoneClientComponent>())
		{
			Ph->SetActiveHomeFromLocation(GetActorLocation());
		}
	}

	// Sla het spel hier op zodat je bij het laden weer bij dit bed begint.
	if (UWorld* W = GetWorld())
	{
		if (UGameInstance* GI = W->GetGameInstance())
		{
			if (USaveGameSubsystem* Save = GI->GetSubsystem<USaveGameSubsystem>()) { Save->SaveGame(true); }
		}
	}
	UWeedToast::NotifyPawn(InstigatorPawn, -1, 2.f, FColor::Cyan, TEXT("Slept - saved here."));
}
