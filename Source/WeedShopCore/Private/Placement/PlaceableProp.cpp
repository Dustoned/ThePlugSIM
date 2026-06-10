#include "Placement/PlaceableProp.h"

#include "Placement/PlaceableTypes.h"
#include "Placement/PropMeshKit.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Net/UnrealNetwork.h"
#include "Game/WeedShopGameState.h"
#include "World/DayCycleComponent.h"
#include "Save/SaveGameSubsystem.h"
#include "Phone/PhoneClientComponent.h"
#include "UI/WeedToast.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Interaction/Interactable.h"
#include "Cultivation/GrowPlant.h"
#include "Cultivation/DryingRack.h"
#include "World/ProcessorMachine.h"
#include "Cultivation/PotTypes.h"

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
}

void APlaceableProp::HideParts()
{
	for (UStaticMeshComponent* P : Parts) { if (P) { P->SetVisibility(false); } }
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
		// Geen mockup voor dit item -> de enkele mesh blijft zichtbaar.
		Mesh->SetVisibility(true);
		HideParts();
	}
}

// Het object (pot/droogrek/hasj-machine) waar deze upgrade-prop bij hoort, of nullptr als 't geen upgrade is.
static AActor* FindUpgradeHostFor(const APlaceableProp* Prop)
{
	if (!Prop || !Prop->GetWorld()) { return nullptr; }
	const FString S = Prop->ItemId.ToString();
	const int32 Kind = (GearUpgradeIndex(Prop->ItemId) >= 0) ? 1 : (S.StartsWith(TEXT("DryUp_")) ? 2 : (S.StartsWith(TEXT("ProcUp_")) ? 3 : 0));
	if (Kind == 0) { return nullptr; }
	const FVector P = Prop->GetActorLocation();
	AActor* Best = nullptr; float BestSq = FMath::Square(180.f);
	auto Consider = [&](AActor* A)
	{
		if (!IsValid(A)) { return; }
		const FVector L = A->GetActorLocation();
		if (FMath::Abs(L.Z - P.Z) > 300.f) { return; }
		const float D = FVector::DistSquared2D(L, P);
		if (D <= BestSq) { BestSq = D; Best = A; }
	};
	if (Kind == 1)      { for (TActorIterator<AGrowPlant> It(Prop->GetWorld()); It; ++It)        { Consider(*It); } }
	else if (Kind == 2) { for (TActorIterator<ADryingRack> It(Prop->GetWorld()); It; ++It)       { Consider(*It); } }
	else if (Kind == 3) { for (TActorIterator<AProcessorMachine> It(Prop->GetWorld()); It; ++It) { Consider(*It); } }
	return Best;
}

FText APlaceableProp::GetInteractionPrompt_Implementation() const
{
	// Upgrade -> gebruik de prompt van het object waar 'ie bij hoort (pot/rek/machine), zodat je via de
	// upgrade direct met de pot kunt werken.
	if (AActor* Host = FindUpgradeHostFor(this)) { return IInteractable::Execute_GetInteractionPrompt(Host); }

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
	const FString Name = bHas ? Def.DisplayName : ItemId.ToString();
	return FText::FromString(FString::Printf(TEXT("%s - hold G to pick up"), *Name));
}

void APlaceableProp::Interact_Implementation(APawn* InstigatorPawn)
{
	// Upgrade -> stuur de interactie door naar het object waar 'ie bij hoort (pot/rek/machine).
	if (AActor* Host = FindUpgradeHostFor(this)) { IInteractable::Execute_Interact(Host, InstigatorPawn); return; }

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
	UWeedToast::Notify(-1, 2.f, FColor::Cyan, TEXT("Slept - saved here."));
}
