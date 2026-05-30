#include "Cultivation/GrowPlant.h"

#include "WeedShopCore.h"
#include "Components/StaticMeshComponent.h"
#include "Data/WeedStrain.h"
#include "Inventory/InventoryComponent.h"
#include "Net/UnrealNetwork.h"

AGrowPlant::AGrowPlant()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	SetRootComponent(Mesh);
	Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
}

void AGrowPlant::BeginPlay()
{
	Super::BeginPlay();

	if (HasAuthority())
	{
		if (const FWeedStrainRow* Strain = GetStrain())
		{
			MaxGrowthSeconds = FMath::Max(1.f, Strain->GrowMinutes * 60.f);
		}
		UpdatePhaseFromGrowth();
	}

	RefreshMesh();
}

void AGrowPlant::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AGrowPlant, StrainId);
	DOREPLIFETIME(AGrowPlant, GrowthSeconds);
	DOREPLIFETIME(AGrowPlant, Phase);
	DOREPLIFETIME(AGrowPlant, CareMultiplier);
}

void AGrowPlant::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!HasAuthority())
	{
		return;
	}

	// Groei op echte tijd.
	if (Phase != EGrowthPhase::Harvestable)
	{
		GrowthSeconds = FMath::Min(GrowthSeconds + DeltaSeconds, MaxGrowthSeconds);
		UpdatePhaseFromGrowth();
	}

	// Langzame droogte-stress als er niet bijgewaterd wordt (geen permanente dood, wel minder yield).
	CareMultiplier = FMath::Clamp(CareMultiplier - DeltaSeconds * 0.002f, 0.3f, 1.0f);
}

void AGrowPlant::UpdatePhaseFromGrowth()
{
	const float F = GetGrowthFraction();
	EGrowthPhase NewPhase;
	if (F >= 1.0f)        { NewPhase = EGrowthPhase::Harvestable; }
	else if (F >= 0.70f)  { NewPhase = EGrowthPhase::Flower; }
	else if (F >= 0.45f)  { NewPhase = EGrowthPhase::PreFlower; }
	else if (F >= 0.15f)  { NewPhase = EGrowthPhase::Vegetative; }
	else                  { NewPhase = EGrowthPhase::Seedling; }

	if (NewPhase != Phase)
	{
		Phase = NewPhase;
		RefreshMesh(); // server-side; clients krijgen het via OnRep_Visual
	}
}

void AGrowPlant::Interact_Implementation(APawn* InstigatorPawn)
{
	// Draait server-authoritative (via de interactie-component).
	if (!HasAuthority())
	{
		return;
	}

	if (Phase == EGrowthPhase::Harvestable)
	{
		Harvest(InstigatorPawn);
	}
	else
	{
		Water();
	}
}

FText AGrowPlant::GetInteractionPrompt_Implementation() const
{
	if (Phase == EGrowthPhase::Harvestable)
	{
		return NSLOCTEXT("WeedShop", "PlantHarvest", "Oogsten");
	}
	return NSLOCTEXT("WeedShop", "PlantWater", "Water geven");
}

void AGrowPlant::Water()
{
	CareMultiplier = FMath::Clamp(CareMultiplier + 0.2f, 0.3f, 1.0f);
	UE_LOG(LogWeedShop, Verbose, TEXT("Plant %s gewaterd -> care %.2f"), *StrainId.ToString(), CareMultiplier);
}

void AGrowPlant::Harvest(APawn* InstigatorPawn)
{
	const FWeedStrainRow* Strain = GetStrain();
	if (!Strain)
	{
		UE_LOG(LogWeedShop, Warning, TEXT("Harvest: strain '%s' niet gevonden in StrainTable."), *StrainId.ToString());
		return;
	}

	const int32 YieldGrams = FMath::Max(1, FMath::RoundToInt(Strain->BaseYieldGrams * CareMultiplier));
	const float ActualThc = Strain->BaseThcPercent * CareMultiplier * FMath::FRandRange(0.9f, 1.1f);

	UInventoryComponent* Inv = InstigatorPawn ? InstigatorPawn->FindComponentByClass<UInventoryComponent>() : nullptr;
	if (Inv && !Strain->HarvestProductId.IsNone())
	{
		Inv->AddItem(Strain->HarvestProductId, YieldGrams);
		UE_LOG(LogWeedShop, Log, TEXT("Oogst: %dg %s (THC %.1f%%) -> inventory van %s"),
			YieldGrams, *Strain->HarvestProductId.ToString(), ActualThc, *GetNameSafe(InstigatorPawn));
	}
	else
	{
		UE_LOG(LogWeedShop, Warning, TEXT("Oogst mislukt: geen InventoryComponent op de speler of HarvestProductId leeg."));
	}

	// Plant is geoogst -> verwijderen (server; repliceert naar clients). Speler plant een nieuw zaadje.
	Destroy();
}

void AGrowPlant::OnRep_Visual()
{
	RefreshMesh();
}

void AGrowPlant::RefreshMesh()
{
	const int32 Index = static_cast<int32>(Phase);
	if (PhaseMeshes.IsValidIndex(Index) && PhaseMeshes[Index])
	{
		Mesh->SetStaticMesh(PhaseMeshes[Index]);
	}
}

const FWeedStrainRow* AGrowPlant::GetStrain() const
{
	if (!StrainTable || StrainId.IsNone())
	{
		return nullptr;
	}
	return StrainTable->FindRow<FWeedStrainRow>(StrainId, TEXT("AGrowPlant::GetStrain"), /*bWarnIfMissing=*/false);
}
