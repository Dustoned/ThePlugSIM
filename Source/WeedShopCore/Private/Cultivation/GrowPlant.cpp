#include "Cultivation/GrowPlant.h"

#include "WeedShopCore.h"
#include "Components/StaticMeshComponent.h"
#include "Data/WeedStrain.h"
#include "Inventory/InventoryComponent.h"
#include "Game/WeedShopGameState.h"
#include "Progression/UpgradeComponent.h"
#include "Progression/StoreComponent.h"
#include "Engine/Engine.h"
#include "Net/UnrealNetwork.h"

AGrowPlant::AGrowPlant()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Root);
	Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);

	// Standaard pot-mesh zodat een geplaatste (lege) pot meteen zichtbaar is, ook zonder
	// PhaseMeshes. Cylinder = 1m; schaal naar een pot van ~50cm breed, 40cm hoog, en til 'm op
	// zodat de onderkant op de actor-origin (= vloer bij plaatsen) staat.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> PotMeshFinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (PotMeshFinder.Succeeded() && !Mesh->GetStaticMesh())
	{
		Mesh->SetStaticMesh(PotMeshFinder.Object);
		Mesh->SetRelativeScale3D(FVector(0.5f, 0.5f, 0.4f));
		Mesh->SetRelativeLocation(FVector(0.f, 0.f, 20.f));
	}

	// StrainTable automatisch koppelen zodat een gespawnde pot kan planten/oogsten zonder BP-setup.
	static ConstructorHelpers::FObjectFinder<UDataTable> StrainTableFinder(TEXT("/Game/_Project/Data/DT_Strains.DT_Strains"));
	if (StrainTableFinder.Succeeded())
	{
		StrainTable = StrainTableFinder.Object;
	}
}

void AGrowPlant::BeginPlay()
{
	Super::BeginPlay();

	if (HasAuthority())
	{
		// Een vooraf-geplaatste plant met een strain telt als geplant.
		if (!StrainId.IsNone())
		{
			bPlanted = true;
		}
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
	DOREPLIFETIME(AGrowPlant, bPlanted);
}

void AGrowPlant::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!HasAuthority())
	{
		return;
	}

	// Lege pot groeit niet.
	if (!bPlanted)
	{
		return;
	}

	// Upgrade-effecten ophalen uit de gedeelde GameState (kweek-gear).
	float GrowthBonus = 0.f;   // bv. LED-lamp/voeding -> sneller
	float CareRetention = 0.f; // bv. betere pot -> minder droogte
	if (const AWeedShopGameState* GS = GetWorld() ? GetWorld()->GetGameState<AWeedShopGameState>() : nullptr)
	{
		if (const UUpgradeComponent* Upg = GS->GetUpgrades())
		{
			GrowthBonus = Upg->GetEffectTotal(TEXT("GrowthSpeed"));
			CareRetention = FMath::Clamp(Upg->GetEffectTotal(TEXT("CareRetention")), 0.f, 0.9f);
		}
	}

	// Groei op echte tijd (versneld door demo-multiplier + kweek-upgrades).
	if (Phase != EGrowthPhase::Harvestable)
	{
		const float Speed = FMath::Max(0.f, GrowthSpeedMultiplier) * (1.f + GrowthBonus);
		GrowthSeconds = FMath::Min(GrowthSeconds + DeltaSeconds * Speed, MaxGrowthSeconds);
		UpdatePhaseFromGrowth();
	}

	// Langzame droogte-stress; een betere pot (CareRetention) vertraagt dit.
	CareMultiplier = FMath::Clamp(CareMultiplier - DeltaSeconds * 0.002f * (1.f - CareRetention), 0.3f, 1.0f);
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

	if (!bPlanted)
	{
		TryPlantFromInventory(InstigatorPawn);
	}
	else if (Phase == EGrowthPhase::Harvestable)
	{
		Harvest(InstigatorPawn);
	}
	else
	{
		Water();
	}
}

bool AGrowPlant::TryPlantFromInventory(APawn* InstigatorPawn)
{
	UInventoryComponent* Inv = InstigatorPawn ? InstigatorPawn->FindComponentByClass<UInventoryComponent>() : nullptr;
	if (!Inv)
	{
		return false;
	}

	// Vind het eerste zaadje in de voorraad.
	FName SeedItem = NAME_None;
	FName StrainToPlant = NAME_None;
	for (const FInventoryStack& Stack : Inv->GetStacks())
	{
		const FName Strain = UStoreComponent::StrainFromSeedItem(Stack.ItemId);
		if (!Strain.IsNone())
		{
			SeedItem = Stack.ItemId;
			StrainToPlant = Strain;
			break;
		}
	}

	if (SeedItem.IsNone() || !Inv->RemoveItem(SeedItem, 1))
	{
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 2.5f, FColor::Orange, TEXT("No seeds in inventory (buy from supplier)."));
		}
		return false;
	}

	// Planten: reset en start de groei voor de gekozen strain.
	StrainId = StrainToPlant;
	bPlanted = true;
	GrowthSeconds = 0.f;
	CareMultiplier = 0.6f;
	if (const FWeedStrainRow* Strain = GetStrain())
	{
		MaxGrowthSeconds = FMath::Max(1.f, Strain->GrowMinutes * 60.f);
	}
	Phase = EGrowthPhase::Seedling;
	RefreshMesh();

	UE_LOG(LogWeedShop, Log, TEXT("Geplant: %s"), *StrainId.ToString());
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor::Green,
			FString::Printf(TEXT("Planted: %s"), *StrainId.ToString()));
	}
	return true;
}

FText AGrowPlant::GetInteractionPrompt_Implementation() const
{
	if (!bPlanted)
	{
		return NSLOCTEXT("WeedShop", "PlantSeed", "Plant a seed");
	}
	if (Phase == EGrowthPhase::Harvestable)
	{
		return FText::FromString(FString::Printf(TEXT("Harvest  (%s)"), *StrainId.ToString()));
	}
	const int32 Pct = FMath::RoundToInt(GetGrowthFraction() * 100.f);
	return FText::FromString(FString::Printf(TEXT("Water  (growth %d%%, care %.0f%%)"),
		Pct, CareMultiplier * 100.f));
}

void AGrowPlant::Water()
{
	CareMultiplier = FMath::Clamp(CareMultiplier + 0.2f, 0.3f, 1.0f);
	UE_LOG(LogWeedShop, Log, TEXT("Plant %s gewaterd -> zorg %.0f%%"), *StrainId.ToString(), CareMultiplier * 100.f);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 2.f, FColor::Cyan,
			FString::Printf(TEXT("Plant watered (care %.0f%%)"), CareMultiplier * 100.f));
	}
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
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 4.f, FColor::Green,
				FString::Printf(TEXT("Harvested: %dg %s (THC %.0f%%)"),
					YieldGrams, *Strain->HarvestProductId.ToString(), ActualThc));
		}
	}
	else
	{
		UE_LOG(LogWeedShop, Warning, TEXT("Oogst mislukt: geen InventoryComponent op de speler of HarvestProductId leeg."));
	}

	// Plant is geoogst -> pot wordt weer leeg en herbruikbaar (plant een nieuw zaadje).
	bPlanted = false;
	StrainId = NAME_None;
	GrowthSeconds = 0.f;
	Phase = EGrowthPhase::Seedling;
	RefreshMesh();
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

float AGrowPlant::GetSecondsRemaining() const
{
	if (!bPlanted || Phase == EGrowthPhase::Harvestable)
	{
		return 0.f;
	}
	const float Speed = FMath::Max(0.01f, GrowthSpeedMultiplier);
	return FMath::Max(0.f, (MaxGrowthSeconds - GrowthSeconds) / Speed);
}

float AGrowPlant::GetEstimatedYieldGrams() const
{
	const FWeedStrainRow* Strain = GetStrain();
	return Strain ? Strain->BaseYieldGrams * CareMultiplier : 0.f;
}

float AGrowPlant::GetEstimatedThcPercent() const
{
	const FWeedStrainRow* Strain = GetStrain();
	return Strain ? Strain->BaseThcPercent * CareMultiplier : 0.f;
}
