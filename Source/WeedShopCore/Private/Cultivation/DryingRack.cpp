#include "Cultivation/DryingRack.h"

#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "Inventory/InventoryComponent.h"
#include "Placement/PlaceableTypes.h"
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
}

void ADryingRack::SetupVisual()
{
	FPlaceableDef Def;
	if (Mesh && GetPlaceableDef(RackTier, Def))
	{
		Mesh->SetWorldScale3D(Def.MeshScale);
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

void ADryingRack::Interact_Implementation(APawn* InstigatorPawn)
{
	if (!HasAuthority()) { return; }
	UInventoryComponent* Inv = InstigatorPawn ? InstigatorPawn->FindComponentByClass<UInventoryComponent>() : nullptr;
	if (!Inv) { return; }

	// 1) Heb je natte wiet in de hand? -> ophangen om te drogen (1 batch per stapel).
	const FName Active = Inv->GetActiveItemId();
	if (Active.ToString().StartsWith(TEXT("WetBud_")))
	{
		if (Entries.Num() >= Capacity())
		{
			if (GEngine) { GEngine->AddOnScreenDebugMessage(-1, 2.5f, FColor::Orange, TEXT("Drying rack is full.")); }
			return;
		}
		const int32 Qty = Inv->GetQuantity(Active);
		if (Qty <= 0) { return; }
		FDryEntry E;
		E.DryItemId = FName(*Active.ToString().RightChop(3)); // "WetBud_X" -> "Bud_X"
		E.Quantity = Qty;
		E.Thc = Inv->GetItemQuality(Active);
		E.Quality = Inv->GetItemQualityPct(Active);
		Inv->RemoveItem(Active, Qty);
		Entries.Add(E);
		UpdateRep();
		if (GEngine) { GEngine->AddOnScreenDebugMessage(-1, 2.5f, FColor(120, 220, 160), FString::Printf(TEXT("Hung %dg to dry."), Qty)); }
		return;
	}

	// 2) Anders: zijn er gedroogde batches? -> oogst ze (met eventueel kwaliteitsverlies bij te lang hangen).
	int32 Collected = 0;
	for (int32 i = Entries.Num() - 1; i >= 0; --i)
	{
		if (!Entries[i].bDone) { continue; }
		const FDryEntry& E = Entries[i];
		const float LossFrac = FMath::Clamp((E.OverTime - DryGraceSeconds) / DryDecayWindow, 0.f, 1.f) * DryMaxLoss;
		const float FinalQ = FMath::Max(1.f, E.Quality * (1.f - LossFrac));
		Inv->AddItem(E.DryItemId, E.Quantity, E.Thc, FinalQ);
		Collected += E.Quantity;
		Entries.RemoveAt(i);
	}
	UpdateRep();
	if (GEngine)
	{
		if (Collected > 0) { GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor::Green, FString::Printf(TEXT("Collected %dg of dried weed."), Collected)); }
		else { GEngine->AddOnScreenDebugMessage(-1, 2.5f, FColor::Orange, TEXT("Nothing dried yet - hang wet weed or wait.")); }
	}
}

FText ADryingRack::GetInteractionPrompt_Implementation() const
{
	if (RepReady > 0)
	{
		return FText::FromString(FString::Printf(TEXT("Collect %d dried batch(es)  (drying %d)"), RepReady, RepDrying));
	}
	if (RepDrying > 0)
	{
		return FText::FromString(FString::Printf(TEXT("Drying %d/%d batch(es)... hold wet weed to add more"), RepDrying, RepCapacity));
	}
	return FText::FromString(FString::Printf(TEXT("Drying rack (0/%d) - hold wet weed to hang it"), RepCapacity));
}
