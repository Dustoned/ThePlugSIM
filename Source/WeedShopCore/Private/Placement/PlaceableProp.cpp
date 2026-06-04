#include "Placement/PlaceableProp.h"

#include "Placement/PlaceableTypes.h"
#include "Placement/PropMeshKit.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Net/UnrealNetwork.h"
#include "Game/WeedShopGameState.h"
#include "World/DayCycleComponent.h"
#include "Save/SaveGameSubsystem.h"
#include "UI/WeedToast.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

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
	else
	{
		// Geen mockup voor dit item -> de enkele mesh blijft zichtbaar.
		Mesh->SetVisibility(true);
		HideParts();
	}
}

FText APlaceableProp::GetInteractionPrompt_Implementation() const
{
	FPlaceableDef Def;
	const bool bHas = GetPlaceableDef(ItemId, Def);
	if (bHas && Def.bIsBed)
	{
		return FText::FromString(TEXT("Sleep (skip to morning)  -  hold G to pick up"));
	}
	const FString Name = bHas ? Def.DisplayName : ItemId.ToString();
	return FText::FromString(FString::Printf(TEXT("%s - hold G to pick up"), *Name));
}

void APlaceableProp::Interact_Implementation(APawn* InstigatorPawn)
{
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
