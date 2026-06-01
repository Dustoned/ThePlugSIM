#include "Cultivation/DryingRack.h"

#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "Inventory/InventoryComponent.h"
#include "Placement/PlaceableTypes.h"
#include "Placement/PropMeshKit.h"
#include "Engine/Engine.h"
#include "Net/UnrealNetwork.h"

namespace
{
	struct FRackDef { const TCHAR* Id; int32 Capacity; float DrySeconds; };
	static const FRackDef GRacks[] = {
		{ TEXT("DryRack_Cheap"),  2, 180.f }, // weinig + langzaam
		{ TEXT("DryRack_Std"),    5, 120.f },
		{ TEXT("DryRack_Pro"),   10,  60.f }, // veel + snel
	};

	constexpr float DryGraceSeconds = 60.f;   // 1 min na klaar mag je 'm laten hangen zonder verlies
	constexpr float DryDecayWindow  = 120.f;  // daarna lineair tot het max-verlies
	constexpr float DryMaxLoss      = 0.10f;  // max 10% kwaliteitsverlies
}

ADryingRack::ADryingRack()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	SetRootComponent(Mesh);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> MatFinder(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (CubeFinder.Succeeded()) { Mesh->SetStaticMesh(CubeFinder.Object); }
	// Rek: breed en hoog, ondiep (~120 x 30 x 150 cm). Exacte schaal komt uit de tier-def.
	Mesh->SetWorldScale3D(FVector(1.2f, 0.3f, 1.5f));
	Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	if (MatFinder.Succeeded())
	{
		if (UMaterialInstanceDynamic* MID = Mesh->CreateDynamicMaterialInstance(0, MatFinder.Object))
		{
			MID->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.55f, 0.42f, 0.28f)); // tan / hout
		}
	}

	Deco = PropKit::MakeDeco(this, Mesh, TEXT("Deco"));
	for (int32 i = 0; i < 10; ++i) { Parts.Add(PropKit::MakePart(this, Deco, *FString::Printf(TEXT("Part%d"), i))); }
}

void ADryingRack::SetupVisual()
{
	FPlaceableDef Def;
	if (!Mesh || !GetPlaceableDef(RackTier, Def)) { return; }
	Mesh->SetWorldScale3D(Def.MeshScale);
	if (Parts.Num() < 10) { return; }

	const float W = Def.MeshScale.X * 100.f; // breedte
	const float D = Def.MeshScale.Y * 100.f; // diepte
	const float H = Def.MeshScale.Z * 100.f; // hoogte
	const float Floor = -H * 0.5f;
	const FLinearColor Frame(0.40f, 0.30f, 0.20f);
	const FLinearColor Bar(0.30f, 0.22f, 0.14f);
	const FLinearColor Mesh2(0.62f, 0.58f, 0.48f);

	Mesh->SetVisibility(false);

	const float Post = FMath::Min(7.f, W * 0.06f);
	const float PX = W * 0.5f - Post * 0.5f;
	// 2 staanders + boven- en onderbalk + achtergaas.
	PropKit::SetPart(Parts[0], PropKit::Cube(), FVector(Post, D, H), FVector(-PX, 0, 0), Frame);
	PropKit::SetPart(Parts[1], PropKit::Cube(), FVector(Post, D, H), FVector( PX, 0, 0), Frame);
	PropKit::SetPart(Parts[2], PropKit::Cube(), FVector(W, D, Post), FVector(0, 0, H * 0.5f - Post * 0.5f), Frame);
	PropKit::SetPart(Parts[3], PropKit::Cube(), FVector(W, D, Post), FVector(0, 0, Floor + Post * 0.5f), Frame);
	PropKit::SetPart(Parts[4], PropKit::Cube(), FVector(W - Post * 2.f, FMath::Min(3.f, D * 0.2f), H - Post * 2.f), FVector(0, D * 0.45f, 0), Mesh2);
	// 5 dwarsstangen (droogroosters) verdeeld over de hoogte.
	const int32 NBars = 5;
	for (int32 b = 0; b < NBars; ++b)
	{
		const float Z = Floor + H * (0.16f + 0.16f * b);
		PropKit::SetPart(Parts[5 + b], PropKit::Cube(), FVector(W - Post * 2.f, D * 0.8f, FMath::Min(3.f, H * 0.02f)), FVector(0, 0, Z), Bar);
	}
}

void ADryingRack::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	SetupVisual();
}

void ADryingRack::OnRep_Tier()
{
	SetupVisual();
}

void ADryingRack::BeginPlay()
{
	Super::BeginPlay();
	SetupVisual();
	if (HasAuthority()) { UpdateRep(); } // capaciteit meteen repliceren (ook bij leeg rek)
}

void ADryingRack::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ADryingRack, RackTier);
	DOREPLIFETIME(ADryingRack, RepDrying);
	DOREPLIFETIME(ADryingRack, RepReady);
	DOREPLIFETIME(ADryingRack, RepCapacity);
	DOREPLIFETIME(ADryingRack, Entries);
}

bool ADryingRack::GetRackDef(FName Tier, int32& OutCapacity, float& OutDrySeconds)
{
	for (const FRackDef& R : GRacks)
	{
		if (Tier == FName(R.Id)) { OutCapacity = R.Capacity; OutDrySeconds = R.DrySeconds; return true; }
	}
	OutCapacity = 2; OutDrySeconds = 180.f; // fallback
	return false;
}

int32 ADryingRack::Capacity() const { int32 C; float D; GetRackDef(RackTier, C, D); return C; }
float ADryingRack::DrySeconds() const { int32 C; float D; GetRackDef(RackTier, C, D); return D; }

void ADryingRack::UpdateRep()
{
	int32 D = 0, R = 0;
	for (const FDryEntry& E : Entries) { if (E.bDone) { ++R; } else { ++D; } }
	if (D != RepDrying || R != RepReady) { RepDrying = D; RepReady = R; }
	RepCapacity = Capacity();
}

void ADryingRack::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!HasAuthority() || Entries.Num() == 0) { return; }

	const float DT = DrySeconds();
	for (FDryEntry& E : Entries)
	{
		if (!E.bDone)
		{
			E.Elapsed += DeltaSeconds;
			if (E.Elapsed >= DT) { E.bDone = true; E.OverTime = 0.f; }
		}
		else
		{
			E.OverTime += DeltaSeconds; // hangt 'ie te lang -> kwaliteit zakt (bij collect verrekend)
		}
	}
	UpdateRep();
}

int32 ADryingRack::ServerHangWet(FName WetId, int32 Qty, float Thc, float QualPct)
{
	if (!HasAuthority() || Qty <= 0) { return 0; }
	if (!WetId.ToString().StartsWith(TEXT("WetBud_"))) { return 0; }
	if (Entries.Num() >= Capacity()) { return 0; }
	FDryEntry E;
	E.DryItemId = FName(*WetId.ToString().RightChop(3)); // "WetBud_X" -> "Bud_X"
	E.Quantity = Qty;
	E.Thc = Thc;
	E.Quality = QualPct;
	Entries.Add(E);
	UpdateRep();
	return Qty;
}

bool ADryingRack::ServerCollectIndex(int32 Index, FName& OutId, int32& OutQty, float& OutThc, float& OutQual)
{
	if (!HasAuthority() || !Entries.IsValidIndex(Index)) { return false; }
	const FDryEntry& E = Entries[Index];
	if (!E.bDone) { return false; }
	const float LossFrac = FMath::Clamp((E.OverTime - DryGraceSeconds) / DryDecayWindow, 0.f, 1.f) * DryMaxLoss;
	OutId = E.DryItemId;
	OutQty = E.Quantity;
	OutThc = E.Thc;
	OutQual = FMath::Max(1.f, E.Quality * (1.f - LossFrac));
	Entries.RemoveAt(Index);
	UpdateRep();
	return true;
}

void ADryingRack::Interact_Implementation(APawn* InstigatorPawn)
{
	// Het droogrek-scherm openen gebeurt lokaal in de character (UI-actie); hier niets te doen.
}

FText ADryingRack::GetInteractionPrompt_Implementation() const
{
	if (RepReady > 0)
	{
		return FText::FromString(FString::Printf(TEXT("Open drying rack  -  %d ready, %d drying"), RepReady, RepDrying));
	}
	if (RepDrying > 0)
	{
		return FText::FromString(FString::Printf(TEXT("Open drying rack  -  drying %d/%d"), RepDrying, RepCapacity));
	}
	return FText::FromString(FString::Printf(TEXT("Open drying rack  (0/%d)"), RepCapacity));
}
