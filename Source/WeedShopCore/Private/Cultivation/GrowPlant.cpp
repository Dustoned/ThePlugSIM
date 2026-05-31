#include "Cultivation/GrowPlant.h"

#include "WeedShopCore.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "Data/WeedStrain.h"
#include "Cultivation/SoilTypes.h"
#include "Cultivation/WaterCanComponent.h"
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

	// Soil-indicatie: bruin schijfje boven in de pot, zichtbaar zodra er soil in zit.
	SoilMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SoilMesh"));
	SoilMesh->SetupAttachment(Root);
	SoilMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	if (PotMeshFinder.Succeeded())
	{
		SoilMesh->SetStaticMesh(PotMeshFinder.Object); // korte, brede cilinder = aarde-laag bovenop
		SoilMesh->SetRelativeScale3D(FVector(0.46f, 0.46f, 0.10f));
		SoilMesh->SetRelativeLocation(FVector(0.f, 0.f, 42.f)); // net boven de potrand (top ~40)
	}
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> SoilMatFinder(TEXT("/Game/_Project/Materials/M_Soil.M_Soil"));
	if (SoilMatFinder.Succeeded())
	{
		SoilMesh->SetMaterial(0, SoilMatFinder.Object);
	}
	SoilMesh->SetVisibility(false);

	// Plantje (kegel) bovenop de pot; schaalt per groeifase.
	PlantMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PlantMesh"));
	PlantMesh->SetupAttachment(Root);
	PlantMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> ConeFinder(TEXT("/Engine/BasicShapes/Cone.Cone"));
	if (ConeFinder.Succeeded())
	{
		PlantMesh->SetStaticMesh(ConeFinder.Object);
	}
	PlantMesh->SetRelativeLocation(FVector(0.f, 0.f, 44.f)); // basis op de aarde-laag
	PlantMesh->SetVisibility(false);

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> PlantMatFinder(TEXT("/Game/_Project/Materials/M_Plant.M_Plant"));
	if (PlantMatFinder.Succeeded()) { PlantMat = PlantMatFinder.Object; }
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> PlantReadyFinder(TEXT("/Game/_Project/Materials/M_PlantReady.M_PlantReady"));
	if (PlantReadyFinder.Succeeded()) { PlantReadyMat = PlantReadyFinder.Object; }
	if (PlantMat) { PlantMesh->SetMaterial(0, PlantMat); }

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
	UpdateSoilVisual();
	UpdatePlantVisual();
}

void AGrowPlant::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AGrowPlant, StrainId);
	DOREPLIFETIME(AGrowPlant, GrowthSeconds);
	DOREPLIFETIME(AGrowPlant, Phase);
	DOREPLIFETIME(AGrowPlant, CareMultiplier);
	DOREPLIFETIME(AGrowPlant, bPlanted);
	DOREPLIFETIME(AGrowPlant, SoilId);
	DOREPLIFETIME(AGrowPlant, SoilUsesLeft);
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
		UpdatePlantVisual();
	}
}

void AGrowPlant::Interact_Implementation(APawn* InstigatorPawn)
{
	// Draait server-authoritative (via de interactie-component).
	if (!HasAuthority())
	{
		return;
	}

	// Hand-gestuurd: kijk wat de speler in z'n hand heeft (actief hotbar-slot).
	UInventoryComponent* Inv = InstigatorPawn ? InstigatorPawn->FindComponentByClass<UInventoryComponent>() : nullptr;
	const FName Hand = Inv ? Inv->GetActiveItemId() : NAME_None;

	if (!bPlanted)
	{
		if (!HasSoil())
		{
			// Soil in de hand nodig.
			if (IsSoilItem(Hand))
			{
				TryAddSoil(InstigatorPawn, Hand);
			}
			else if (GEngine)
			{
				GEngine->AddOnScreenDebugMessage(-1, 2.5f, FColor::Orange, TEXT("Hold soil in your hand (1-8) to fill the pot."));
			}
		}
		else
		{
			// Zaadje in de hand nodig.
			if (!UStoreComponent::StrainFromSeedItem(Hand).IsNone())
			{
				TryPlantFromInventory(InstigatorPawn, Hand);
			}
			else if (GEngine)
			{
				GEngine->AddOnScreenDebugMessage(-1, 2.5f, FColor::Orange, TEXT("Hold a seed in your hand (1-8) to plant."));
			}
		}
	}
	else if (Phase == EGrowthPhase::Harvestable)
	{
		Harvest(InstigatorPawn);
	}
	else
	{
		// Waterfles in de hand nodig.
		if (Hand.ToString().StartsWith(TEXT("WaterBottle")))
		{
			Water(InstigatorPawn);
		}
		else if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 2.5f, FColor::Orange, TEXT("Hold your water bottle in your hand (1-8) to water."));
		}
	}
}

bool AGrowPlant::TryAddSoil(APawn* InstigatorPawn, FName SoilItem)
{
	UInventoryComponent* Inv = InstigatorPawn ? InstigatorPawn->FindComponentByClass<UInventoryComponent>() : nullptr;
	FSoilDef Def;
	if (!Inv || !GetSoilDef(SoilItem, Def) || !Inv->RemoveItem(SoilItem, 1))
	{
		return false;
	}

	SoilId = Def.ItemId;
	SoilUsesLeft = Def.Harvests;
	UpdateSoilVisual();
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor::Green,
			FString::Printf(TEXT("Soil added: %s (%d harvests). Now plant a seed."), *Def.DisplayName, SoilUsesLeft));
	}
	return true;
}

bool AGrowPlant::TryPlantFromInventory(APawn* InstigatorPawn, FName SeedItem)
{
	UInventoryComponent* Inv = InstigatorPawn ? InstigatorPawn->FindComponentByClass<UInventoryComponent>() : nullptr;
	const FName StrainToPlant = UStoreComponent::StrainFromSeedItem(SeedItem);
	if (!Inv || StrainToPlant.IsNone() || !Inv->RemoveItem(SeedItem, 1))
	{
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
	UpdatePlantVisual();

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
		return HasSoil()
			? NSLOCTEXT("WeedShop", "PlantSeed", "Hold a seed + E to plant")
			: NSLOCTEXT("WeedShop", "AddSoil", "Hold soil + E to fill the pot");
	}
	if (Phase == EGrowthPhase::Harvestable)
	{
		return FText::FromString(FString::Printf(TEXT("Harvest  (%s)"), *StrainId.ToString()));
	}
	const int32 Pct = FMath::RoundToInt(GetGrowthFraction() * 100.f);
	return FText::FromString(FString::Printf(TEXT("Hold bottle + E to water  (growth %d%%, care %.0f%%)"),
		Pct, CareMultiplier * 100.f));
}

void AGrowPlant::Water(APawn* InstigatorPawn)
{
	UWaterCanComponent* Can = InstigatorPawn ? InstigatorPawn->FindComponentByClass<UWaterCanComponent>() : nullptr;
	if (!Can || !Can->HasBottle())
	{
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 2.5f, FColor::Orange, TEXT("You need a water bottle (buy one from the supplier)."));
		}
		return;
	}
	if (!Can->TryUseCharge())
	{
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 2.5f, FColor::Orange, TEXT("Water bottle is empty — fill it at the sink."));
		}
		return;
	}

	CareMultiplier = FMath::Clamp(CareMultiplier + 0.2f, 0.3f, 1.0f);
	UE_LOG(LogWeedShop, Log, TEXT("Plant %s gewaterd -> zorg %.0f%%"), *StrainId.ToString(), CareMultiplier * 100.f);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 2.f, FColor::Cyan,
			FString::Printf(TEXT("Plant watered (care %.0f%%, water left %d/%d)"),
				CareMultiplier * 100.f, Can->GetCharges(), Can->GetMaxCharges()));
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

	// Soil-bonus op yield + kwaliteit.
	float SoilYield = 1.f, SoilQuality = 1.f;
	FSoilDef SoilDef;
	if (GetSoilDef(SoilId, SoilDef)) { SoilYield = SoilDef.YieldMult; SoilQuality = SoilDef.QualityMult; }

	const int32 YieldGrams = FMath::Max(1, FMath::RoundToInt(Strain->BaseYieldGrams * CareMultiplier * SoilYield));
	const float ActualThc = Strain->BaseThcPercent * CareMultiplier * SoilQuality * FMath::FRandRange(0.9f, 1.1f);

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

	// Soil verbruikt een oogst; raakt 'ie op, dan moet er nieuwe soil in.
	if (SoilUsesLeft > 0)
	{
		SoilUsesLeft--;
	}
	if (SoilUsesLeft <= 0)
	{
		SoilId = NAME_None;
		UpdateSoilVisual();
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 4.f, FColor::Orange, TEXT("The soil is used up — add fresh soil before replanting."));
		}
	}

	// Plant is geoogst -> pot wordt weer leeg en herbruikbaar (zelfde soil tot 'ie op is).
	bPlanted = false;
	StrainId = NAME_None;
	GrowthSeconds = 0.f;
	Phase = EGrowthPhase::Seedling;
	RefreshMesh();
	UpdatePlantVisual();
}

void AGrowPlant::OnRep_Visual()
{
	RefreshMesh();
	UpdatePlantVisual();
}

void AGrowPlant::OnRep_Soil()
{
	UpdateSoilVisual();
}

void AGrowPlant::UpdateSoilVisual()
{
	if (SoilMesh)
	{
		SoilMesh->SetVisibility(HasSoil());
	}
}

void AGrowPlant::UpdatePlantVisual()
{
	if (!PlantMesh)
	{
		return;
	}
	if (!bPlanted)
	{
		PlantMesh->SetVisibility(false);
		return;
	}
	PlantMesh->SetVisibility(true);

	// Schaal per fase: zaailing klein -> volgroeid groot.
	FVector Scale;
	switch (Phase)
	{
	case EGrowthPhase::Seedling:    Scale = FVector(0.18f, 0.18f, 0.12f); break;
	case EGrowthPhase::Vegetative:  Scale = FVector(0.26f, 0.26f, 0.32f); break;
	case EGrowthPhase::PreFlower:   Scale = FVector(0.34f, 0.34f, 0.50f); break;
	case EGrowthPhase::Flower:      Scale = FVector(0.42f, 0.42f, 0.64f); break;
	default:                        Scale = FVector(0.48f, 0.48f, 0.74f); break; // Harvestable
	}
	PlantMesh->SetRelativeScale3D(Scale);

	// Rijpe kleur bij oogstklaar.
	UMaterialInterface* Mat = (Phase == EGrowthPhase::Harvestable && PlantReadyMat) ? PlantReadyMat : PlantMat;
	if (Mat)
	{
		PlantMesh->SetMaterial(0, Mat);
	}
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
	if (!Strain) { return 0.f; }
	FSoilDef S; const float SoilMult = GetSoilDef(SoilId, S) ? S.YieldMult : 1.f;
	return Strain->BaseYieldGrams * CareMultiplier * SoilMult;
}

float AGrowPlant::GetEstimatedThcPercent() const
{
	const FWeedStrainRow* Strain = GetStrain();
	if (!Strain) { return 0.f; }
	FSoilDef S; const float SoilMult = GetSoilDef(SoilId, S) ? S.QualityMult : 1.f;
	return Strain->BaseThcPercent * CareMultiplier * SoilMult;
}
