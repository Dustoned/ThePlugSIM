#include "Cultivation/GrowPlant.h"

#include "WeedShopCore.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "Data/WeedStrain.h"
#include "Cultivation/SoilTypes.h"
#include "Cultivation/PotTypes.h"
#include "Cultivation/WaterCanComponent.h"
#include "Inventory/InventoryComponent.h"
#include "Game/WeedShopGameState.h"
#include "Progression/UpgradeComponent.h"
#include "Progression/StoreComponent.h"
#include "Progression/LevelComponent.h"
#include "Engine/Engine.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

AGrowPlant::AGrowPlant()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Root);
	Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> PotMeshFinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (PotMeshFinder.Succeeded())
	{
		Mesh->SetStaticMesh(PotMeshFinder.Object);
		Mesh->SetRelativeScale3D(FVector(0.5f, 0.5f, 0.4f));
		Mesh->SetRelativeLocation(FVector(0.f, 0.f, 20.f));
	}

	SoilMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SoilMesh"));
	SoilMesh->SetupAttachment(Root);
	SoilMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	if (PotMeshFinder.Succeeded())
	{
		SoilMesh->SetStaticMesh(PotMeshFinder.Object);
		SoilMesh->SetRelativeScale3D(FVector(0.46f, 0.46f, 0.10f));
		SoilMesh->SetRelativeLocation(FVector(0.f, 0.f, 42.f));
	}
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> SoilMatFinder(TEXT("/Game/_Project/Materials/M_Soil.M_Soil"));
	if (SoilMatFinder.Succeeded()) { SoilMesh->SetMaterial(0, SoilMatFinder.Object); }
	SoilMesh->SetVisibility(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> ConeFinder(TEXT("/Engine/BasicShapes/Cone.Cone"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> PlantMatFinder(TEXT("/Game/_Project/Materials/M_Plant.M_Plant"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> PlantReadyFinder(TEXT("/Game/_Project/Materials/M_PlantReady.M_PlantReady"));
	if (PlantMatFinder.Succeeded()) { PlantMat = PlantMatFinder.Object; }
	if (PlantReadyFinder.Succeeded()) { PlantReadyMat = PlantReadyFinder.Object; }

	// Eén plant-mesh per mogelijke plek.
	for (int32 i = 0; i < MaxSlots; ++i)
	{
		UStaticMeshComponent* PM = CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("Plant%d"), i));
		PM->SetupAttachment(Root);
		PM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		if (ConeFinder.Succeeded()) { PM->SetStaticMesh(ConeFinder.Object); }
		if (PlantMat) { PM->SetMaterial(0, PlantMat); }
		PM->SetVisibility(false);
		PlantMeshes.Add(PM);
	}

	static ConstructorHelpers::FObjectFinder<UDataTable> StrainTableFinder(TEXT("/Game/_Project/Data/DT_Strains.DT_Strains"));
	if (StrainTableFinder.Succeeded()) { StrainTable = StrainTableFinder.Object; }
}

void AGrowPlant::BeginPlay()
{
	Super::BeginPlay();

	if (HasAuthority())
	{
		EnsureSlots();
		CareMultiplier = FMath::Min(0.6f, GetMaxCare());
		CareAvg = CareMultiplier;
	}

	UpdatePotVisual();
	UpdateSoilVisual();
	UpdatePlantVisual();
}

void AGrowPlant::CaptureState(FGrowPlantState& Out) const
{
	Out.PotUpgradeMask = PotUpgradeMask;
	Out.SoilId = SoilId;
	Out.SoilUsesLeft = SoilUsesLeft;
	Out.CareMultiplier = CareMultiplier;
	Out.CareAvg = CareAvg;
	Out.WaterLevel = WaterLevel;
	Out.SlotStrain = SlotStrain;
	Out.SlotGrowth = SlotGrowth;
	Out.SlotPhase.Reset();
	for (EGrowthPhase P : SlotPhase) { Out.SlotPhase.Add((uint8)P); }
}

void AGrowPlant::RestoreState(const FGrowPlantState& In)
{
	if (!HasAuthority()) { return; }
	EnsureSlots(); // arrays op tier-maat
	PotUpgradeMask = In.PotUpgradeMask;
	SoilId = In.SoilId;
	SoilUsesLeft = In.SoilUsesLeft;
	CareMultiplier = In.CareMultiplier;
	CareAvg = In.CareAvg;
	CareSum = In.CareAvg; CareTime = 1.f; // benadering zodat het gemiddelde stabiel doorloopt
	WaterLevel = In.WaterLevel;

	const int32 N = SlotStrain.Num();
	for (int32 i = 0; i < N; ++i)
	{
		SlotStrain[i] = In.SlotStrain.IsValidIndex(i) ? In.SlotStrain[i] : NAME_None;
		SlotGrowth[i] = In.SlotGrowth.IsValidIndex(i) ? In.SlotGrowth[i] : 0.f;
		SlotPhase[i] = In.SlotPhase.IsValidIndex(i) ? (EGrowthPhase)In.SlotPhase[i] : EGrowthPhase::Seedling;
	}
	UpdatePotVisual();
	UpdateSoilVisual();
	UpdatePlantVisual();
	UpdatePhases();
}

void AGrowPlant::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AGrowPlant, PotTier);
	DOREPLIFETIME(AGrowPlant, PotUpgradeMask);
	DOREPLIFETIME(AGrowPlant, SlotStrain);
	DOREPLIFETIME(AGrowPlant, SlotGrowth);
	DOREPLIFETIME(AGrowPlant, SlotPhase);
	DOREPLIFETIME(AGrowPlant, CareMultiplier);
	DOREPLIFETIME(AGrowPlant, CareAvg);
	DOREPLIFETIME(AGrowPlant, WaterLevel);
	DOREPLIFETIME(AGrowPlant, SoilId);
	DOREPLIFETIME(AGrowPlant, SoilUsesLeft);
}

int32 AGrowPlant::SlotCapacityForTier() const
{
	FPotDef Pot;
	if (GetPotDef(PotTier, Pot)) { return FMath::Clamp(Pot.PlantSlots, 1, MaxSlots); }
	return 1;
}

void AGrowPlant::EnsureSlots()
{
	const int32 N = SlotCapacityForTier();
	if (SlotStrain.Num() != N)
	{
		SlotStrain.Init(NAME_None, N);
		SlotGrowth.Init(0.f, N);
		SlotPhase.Init(EGrowthPhase::Seedling, N);
	}
}

float AGrowPlant::SlotMaxSeconds(int32 Slot) const
{
	if (const FWeedStrainRow* Row = SlotStrain.IsValidIndex(Slot) ? GetStrainRow(SlotStrain[Slot]) : nullptr)
	{
		return FMath::Max(1.f, Row->GrowMinutes * 60.f);
	}
	return 240.f;
}

const FWeedStrainRow* AGrowPlant::GetStrainRow(FName StrainId) const
{
	if (!StrainTable || StrainId.IsNone()) { return nullptr; }
	return StrainTable->FindRow<FWeedStrainRow>(StrainId, TEXT("AGrowPlant::GetStrainRow"), false);
}

void AGrowPlant::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!HasAuthority() || GetPlantedCount() == 0)
	{
		return;
	}

	float GrowthBonus = 0.f, CareRetention = 0.f;
	if (const AWeedShopGameState* GS = GetWorld() ? GetWorld()->GetGameState<AWeedShopGameState>() : nullptr)
	{
		if (const UUpgradeComponent* Upg = GS->GetUpgrades())
		{
			GrowthBonus = Upg->GetEffectTotal(TEXT("GrowthSpeed"));
			CareRetention = FMath::Clamp(Upg->GetEffectTotal(TEXT("CareRetention")), 0.f, 0.9f);
		}
	}

	// Groei per plek (grow-lamp = +30% sneller).
	const float LampMul = HasPotUpgrade(4) ? 1.3f : 1.f;
	const float Speed = FMath::Max(0.f, GrowthSpeedMultiplier) * (1.f + GrowthBonus) * LampMul;
	for (int32 i = 0; i < SlotStrain.Num(); ++i)
	{
		if (SlotStrain[i].IsNone() || SlotPhase[i] == EGrowthPhase::Harvestable) { continue; }
		SlotGrowth[i] = FMath::Min(SlotGrowth[i] + DeltaSeconds * Speed, SlotMaxSeconds(i));
	}
	UpdatePhases();

	// Waterpeil loopt langzaam leeg (isolatie-upgrade + pot-retentie vertragen dit).
	// Auto-watering houdt de pot vanzelf vol -> geen handmatig water nodig.
	const float MaxCare = GetMaxCare();
	if (HasPotUpgrade(5))
	{
		WaterLevel = 1.f;
	}
	else
	{
		const float LeakMul = HasPotUpgrade(1) ? 0.5f : 1.f;
		WaterLevel = FMath::Clamp(WaterLevel - DeltaSeconds * 0.02f * (1.f - CareRetention * 0.5f) * LeakMul, 0.f, 1.f);
	}

	const bool bAnyReady = GetReadyCount() > 0;
	if (!bAnyReady)
	{
		// Normaal: gezondheid/care volgt het waterpeil. Genoeg water -> stijgt richting het pot-plafond;
		// te droog -> zakt. Last-minute water geven herstelt de gemiddelde kwaliteit dus niet.
		const float Target = (WaterLevel >= 0.25f) ? MaxCare : 0.3f;
		CareMultiplier = FMath::Clamp(FMath::FInterpTo(CareMultiplier, Target, DeltaSeconds, 0.2f), 0.3f, MaxCare);

		// Kwaliteit = tijd-gewogen gemiddelde gezondheid.
		CareSum += CareMultiplier * DeltaSeconds;
		CareTime += DeltaSeconds;
		CareAvg = (CareTime > 0.f) ? (CareSum / CareTime) : CareMultiplier;
	}
	else
	{
		// Over-rijp: niet geoogst -> de gezondheid (health) zakt. Het bulk-deel (tot 10%) gaat normaal,
		// de laatste 10% (10% -> 0%) gaat extra langzaam. Op 0% kwaliteit sterft de plant en is het
		// zaadje weg. Verval schaalt mee met de groeisnelheid zodat het in de demo niet té snel gaat.
		float RefSecs = 240.f;
		for (int32 i = 0; i < SlotStrain.Num(); ++i)
		{
			if (!SlotStrain[i].IsNone() && SlotPhase[i] == EGrowthPhase::Harvestable)
			{
				RefSecs = FMath::Max(RefSecs, SlotMaxSeconds(i));
			}
		}
		const float BulkRate = 0.90f / FMath::Max(1.f, RefSecs * RotBulkFactor); // health per versnelde sec
		const float SlowRate = 0.10f / FMath::Max(1.f, RefSecs * RotSlowFactor);
		const float Rate = (CareMultiplier > 0.10f) ? BulkRate : SlowRate;
		CareMultiplier = FMath::Max(0.f, CareMultiplier - DeltaSeconds * Speed * Rate);

		// Kwaliteit volgt de zakkende gezondheid mee naar beneden (kan niet meer stijgen).
		CareAvg = FMath::Min(CareAvg, CareMultiplier);

		if (CareMultiplier <= 0.f)
		{
			// Plant(en) sterven: alle oogstklare plekken leeg, zaadjes weg.
			bool bDied = false;
			for (int32 i = 0; i < SlotStrain.Num(); ++i)
			{
				if (SlotStrain[i].IsNone() || SlotPhase[i] != EGrowthPhase::Harvestable) { continue; }
				SlotStrain[i] = NAME_None;
				SlotGrowth[i] = 0.f;
				SlotPhase[i] = EGrowthPhase::Seedling;
				bDied = true;
			}
			if (bDied)
			{
				// Reset de meting voor wat er nog (groeiend) in de pot staat.
				CareMultiplier = FMath::Min(0.3f, MaxCare);
				CareSum = 0.f; CareTime = 0.f; CareAvg = CareMultiplier;
				UpdatePlantVisual();
				if (GEngine) { GEngine->AddOnScreenDebugMessage(-1, 4.f, FColor::Red, TEXT("A plant rotted away - harvested too late. Seed lost.")); }
			}
		}
	}
}

void AGrowPlant::UpdatePhases()
{
	bool bChanged = false;
	for (int32 i = 0; i < SlotStrain.Num(); ++i)
	{
		if (SlotStrain[i].IsNone()) { continue; }
		const float F = SlotMaxSeconds(i) > 0.f ? SlotGrowth[i] / SlotMaxSeconds(i) : 0.f;
		EGrowthPhase NewPhase;
		if (F >= 1.0f)       { NewPhase = EGrowthPhase::Harvestable; }
		else if (F >= 0.70f) { NewPhase = EGrowthPhase::Flower; }
		else if (F >= 0.45f) { NewPhase = EGrowthPhase::PreFlower; }
		else if (F >= 0.15f) { NewPhase = EGrowthPhase::Vegetative; }
		else                 { NewPhase = EGrowthPhase::Seedling; }
		if (NewPhase != SlotPhase[i]) { SlotPhase[i] = NewPhase; bChanged = true; }
	}
	if (bChanged) { UpdatePlantVisual(); }
}

FText AGrowPlant::GetPrimaryStrainName() const
{
	for (const FName& S : SlotStrain)
	{
		if (S.IsNone()) { continue; }
		if (const FWeedStrainRow* Row = GetStrainRow(S)) { return Row->DisplayName; }
		return FText::FromName(S);
	}
	return FText::GetEmpty();
}

float AGrowPlant::GetPrimaryBaseThc() const
{
	for (const FName& S : SlotStrain)
	{
		if (S.IsNone()) { continue; }
		if (const FWeedStrainRow* Row = GetStrainRow(S)) { return Row->BaseThcPercent; }
	}
	return 0.f;
}

float AGrowPlant::GetSlotFraction(int32 Slot) const
{
	if (!SlotStrain.IsValidIndex(Slot) || SlotStrain[Slot].IsNone())
	{
		return 0.f;
	}
	const float MaxS = SlotMaxSeconds(Slot);
	return MaxS > 0.f ? FMath::Clamp(SlotGrowth[Slot] / MaxS, 0.f, 1.f) : 0.f;
}

int32 AGrowPlant::GetPlantedCount() const
{
	int32 C = 0;
	for (const FName& S : SlotStrain) { if (!S.IsNone()) { ++C; } }
	return C;
}

int32 AGrowPlant::GetReadyCount() const
{
	int32 C = 0;
	for (int32 i = 0; i < SlotStrain.Num(); ++i)
	{
		if (!SlotStrain[i].IsNone() && SlotPhase[i] == EGrowthPhase::Harvestable) { ++C; }
	}
	return C;
}

void AGrowPlant::Interact_Implementation(APawn* InstigatorPawn)
{
	if (!HasAuthority()) { return; }

	UInventoryComponent* Inv = InstigatorPawn ? InstigatorPawn->FindComponentByClass<UInventoryComponent>() : nullptr;
	const FName Hand = Inv ? Inv->GetActiveItemId() : NAME_None;

	// 1) Oogstklare planten? Oogst alles tegelijk.
	if (GetReadyCount() > 0)
	{
		HarvestReady(InstigatorPawn);
		return;
	}
	// 2) Geen soil? Soil in de hand toevoegen.
	if (!HasSoil())
	{
		if (IsSoilItem(Hand)) { TryAddSoil(InstigatorPawn, Hand); }
		else if (GEngine) { GEngine->AddOnScreenDebugMessage(-1, 2.5f, FColor::Orange, TEXT("Hold soil in your hand (1-8) to fill the pot.")); }
		return;
	}
	// 3) Lege plek + zaadje in de hand -> planten.
	if (!UStoreComponent::StrainFromSeedItem(Hand).IsNone() && GetPlantedCount() < GetNumSlots())
	{
		TryPlantNextSlot(InstigatorPawn, Hand);
		return;
	}
	// 4) Waterfles in de hand -> hele pot water geven.
	if (Hand.ToString().StartsWith(TEXT("WaterBottle")))
	{
		WaterAll(InstigatorPawn);
		return;
	}
	// Anders: hint.
	if (GEngine)
	{
		const FString Msg = (GetPlantedCount() < GetNumSlots())
			? TEXT("Hold a seed (1-8) to plant, or your water bottle to water.")
			: TEXT("Pot is full. Hold your water bottle (1-8) to water.");
		GEngine->AddOnScreenDebugMessage(-1, 2.5f, FColor::Orange, Msg);
	}
}

bool AGrowPlant::TryAddSoil(APawn* InstigatorPawn, FName SoilItem)
{
	UInventoryComponent* Inv = InstigatorPawn ? InstigatorPawn->FindComponentByClass<UInventoryComponent>() : nullptr;
	FSoilDef Def;
	if (!Inv || !GetSoilDef(SoilItem, Def) || !Inv->RemoveItem(SoilItem, 1)) { return false; }

	SoilId = Def.ItemId;
	SoilUsesLeft = Def.Harvests;
	UpdateSoilVisual();
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor::Green,
			FString::Printf(TEXT("Soil added: %s (%d harvests). Now plant seeds."), *Def.DisplayName, SoilUsesLeft));
	}
	return true;
}

bool AGrowPlant::TryPlantNextSlot(APawn* InstigatorPawn, FName SeedItem)
{
	UInventoryComponent* Inv = InstigatorPawn ? InstigatorPawn->FindComponentByClass<UInventoryComponent>() : nullptr;
	const FName StrainToPlant = UStoreComponent::StrainFromSeedItem(SeedItem);
	if (!Inv || StrainToPlant.IsNone()) { return false; }

	int32 Empty = INDEX_NONE;
	for (int32 i = 0; i < SlotStrain.Num(); ++i) { if (SlotStrain[i].IsNone()) { Empty = i; break; } }
	if (Empty == INDEX_NONE || !Inv->RemoveItem(SeedItem, 1)) { return false; }

	// Eerste plant in een lege pot: reset de verzorging-meting.
	if (GetPlantedCount() == 0)
	{
		CareMultiplier = FMath::Min(0.6f, GetMaxCare());
		CareSum = 0.f; CareTime = 0.f; CareAvg = CareMultiplier;
		WaterLevel = 0.6f;
	}

	SlotStrain[Empty] = StrainToPlant;
	SlotGrowth[Empty] = 0.f;
	SlotPhase[Empty] = EGrowthPhase::Seedling;
	UpdatePlantVisual();
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor::Green,
			FString::Printf(TEXT("Planted: %s  (%d/%d)"), *StrainToPlant.ToString(), GetPlantedCount(), GetNumSlots()));
	}
	return true;
}

void AGrowPlant::WaterAll(APawn* InstigatorPawn)
{
	if (GetPlantedCount() == 0)
	{
		if (GEngine) { GEngine->AddOnScreenDebugMessage(-1, 2.f, FColor::Orange, TEXT("Nothing planted to water yet.")); }
		return;
	}
	UWaterCanComponent* Can = InstigatorPawn ? InstigatorPawn->FindComponentByClass<UWaterCanComponent>() : nullptr;
	if (!Can || !Can->HasBottle())
	{
		if (GEngine) { GEngine->AddOnScreenDebugMessage(-1, 2.5f, FColor::Orange, TEXT("You need a water bottle (buy one from the supplier).")); }
		return;
	}
	if (!Can->TryUseCharge())
	{
		if (GEngine) { GEngine->AddOnScreenDebugMessage(-1, 2.5f, FColor::Orange, TEXT("Water bottle is empty - fill it at the sink.")); }
		return;
	}
	WaterLevel = FMath::Clamp(WaterLevel + 0.6f, 0.f, 1.f);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 2.f, FColor::Cyan,
			FString::Printf(TEXT("Pot watered (water %.0f%%, bottle %d/%d)"), WaterLevel * 100.f, Can->GetCharges(), Can->GetMaxCharges()));
	}
}

void AGrowPlant::HarvestReady(APawn* InstigatorPawn)
{
	UInventoryComponent* Inv = InstigatorPawn ? InstigatorPawn->FindComponentByClass<UInventoryComponent>() : nullptr;
	if (!Inv) { return; }

	float SoilYield = 1.f, SoilQuality = 1.f;
	FSoilDef SoilDef;
	if (GetSoilDef(SoilId, SoilDef)) { SoilYield = SoilDef.YieldMult; SoilQuality = SoilDef.QualityMult; }
	float PotYield = 1.f;
	FPotDef PotDef;
	if (GetPotDef(PotTier, PotDef)) { PotYield = PotDef.YieldMult; }
	if (HasPotUpgrade(2)) { PotYield *= 1.2f; }

	int32 Harvested = 0, TotalGrams = 0;
	for (int32 i = 0; i < SlotStrain.Num(); ++i)
	{
		if (SlotStrain[i].IsNone() || SlotPhase[i] != EGrowthPhase::Harvestable) { continue; }
		const FWeedStrainRow* Row = GetStrainRow(SlotStrain[i]);
		if (!Row || Row->HarvestProductId.IsNone()) { continue; }

		// Kwaliteit (0..1) = verzorging x soil-kwaliteit. Bepaalt zowel opbrengst als THC%.
		const float CareQ = FMath::Clamp(CareAvg, 0.f, 1.f);
		const float QualityFrac = FMath::Clamp(CareQ * SoilQuality, 0.f, 1.f);
		const int32 YieldGrams = FMath::Max(1, FMath::RoundToInt(Row->BaseYieldGrams * CareQ * SoilYield * PotYield));

		// THC% afgeleid van strain-potentie x kwaliteit. Wiet heeft ALTIJD THC% (floor), 0% kan niet:
		// slecht verzorgd = gewoon zwakke wiet, geen "geen wiet". Op hele % afgerond zodat oogsten van
		// dezelfde kwaliteit netjes samen stapelen (en alleen echt andere batches apart blijven).
		const float ThcRaw = Row->BaseThcPercent * QualityFrac * FMath::FRandRange(0.97f, 1.03f);
		const float ThcPercent = FMath::RoundToFloat(FMath::Max(Row->BaseThcPercent * 0.15f, FMath::Max(1.0f, ThcRaw)));
		const float QualityPct = FMath::RoundToFloat(FMath::Max(5.f, QualityFrac * 100.f));

		// Vers geoogst = NAT: je krijgt "WetBud_<strain>", die moet eerst drogen op een droogrek
		// voordat het verkoopbaar/rookbaar wordt.
		const FName WetId(*FString::Printf(TEXT("Wet%s"), *Row->HarvestProductId.ToString()));
		Inv->AddItem(WetId, YieldGrams, ThcPercent, QualityPct);
		TotalGrams += YieldGrams;
		++Harvested;

		// Plek weer leeg.
		SlotStrain[i] = NAME_None;
		SlotGrowth[i] = 0.f;
		SlotPhase[i] = EGrowthPhase::Seedling;
	}

	if (Harvested > 0)
	{
		// XP voor het oogsten: per gram + bonus per plant.
		if (const AWeedShopGameState* GS = GetWorld() ? GetWorld()->GetGameState<AWeedShopGameState>() : nullptr)
		{
			if (ULevelComponent* Lv = GS->GetLeveling())
			{
				Lv->AddXP(Harvested * 10 + TotalGrams);
			}
		}

		// Eén oogst-actie verbruikt één soil-lading.
		if (SoilUsesLeft > 0) { SoilUsesLeft--; }
		if (SoilUsesLeft <= 0)
		{
			SoilId = NAME_None;
			UpdateSoilVisual();
		}
		UpdatePlantVisual();
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 4.f, FColor::Green,
				FString::Printf(TEXT("Harvested %d plant(s): %dg total"), Harvested, TotalGrams));
			if (SoilId.IsNone())
			{
				GEngine->AddOnScreenDebugMessage(-1, 4.f, FColor::Orange, TEXT("The soil is used up - add fresh soil before replanting."));
			}
		}
	}
}

FText AGrowPlant::GetInteractionPrompt_Implementation() const
{
	if (GetReadyCount() > 0)
	{
		// Kwaliteit (gezondheid) zakt zolang je niet oogst -> waarschuwen.
		const float Q = FMath::Clamp(CareAvg, 0.f, 1.f) * 100.f;
		const FString Warn = (Q < 60.f) ? TEXT("  - harvest now, quality dropping!") : TEXT("");
		return FText::FromString(FString::Printf(TEXT("Harvest %d ready plant(s)  (quality %.0f%%)%s"),
			GetReadyCount(), Q, *Warn));
	}
	if (!HasSoil())
	{
		return NSLOCTEXT("WeedShop", "AddSoil", "Add soil (hold soil)");
	}
	if (GetPlantedCount() < GetNumSlots())
	{
		return FText::FromString(FString::Printf(TEXT("Plant a seed (hold seed)  (%d/%d)"), GetPlantedCount(), GetNumSlots()));
	}
	return FText::FromString(FString::Printf(TEXT("Water the plant (hold bottle)  (water %.0f%%)"), WaterLevel * 100.f));
}

float AGrowPlant::GetMaxCare() const
{
	float Cap = 0.7f;
	FPotDef Pot;
	if (GetPotDef(PotTier, Pot)) { Cap = Pot.CareCap; }

	float CareRetention = 0.f;
	if (const AWeedShopGameState* GS = GetWorld() ? GetWorld()->GetGameState<AWeedShopGameState>() : nullptr)
	{
		if (const UUpgradeComponent* Upg = GS->GetUpgrades())
		{
			CareRetention = FMath::Clamp(Upg->GetEffectTotal(TEXT("CareRetention")), 0.f, 0.9f);
		}
	}
	const float Drainage = HasPotUpgrade(0) ? 0.10f : 0.f;
	const float Tent = HasPotUpgrade(3) ? 0.15f : 0.f; // grow-tent: hoger kwaliteitsplafond
	return FMath::Clamp(Cap + CareRetention * 0.3f + Drainage + Tent, 0.4f, 1.0f);
}

float AGrowPlant::GetSecondsRemaining() const
{
	float Best = -1.f;
	const float Spd = FMath::Max(0.01f, GrowthSpeedMultiplier);
	for (int32 i = 0; i < SlotStrain.Num(); ++i)
	{
		if (SlotStrain[i].IsNone() || SlotPhase[i] == EGrowthPhase::Harvestable) { continue; }
		const float Rem = FMath::Max(0.f, (SlotMaxSeconds(i) - SlotGrowth[i]) / Spd);
		if (Best < 0.f || Rem < Best) { Best = Rem; }
	}
	return Best < 0.f ? 0.f : Best;
}

float AGrowPlant::GetEstimatedTotalYield() const
{
	FSoilDef S; const float SoilMult = GetSoilDef(SoilId, S) ? S.YieldMult : 1.f;
	FPotDef P; float PotMult = GetPotDef(PotTier, P) ? P.YieldMult : 1.f;
	if (HasPotUpgrade(2)) { PotMult *= 1.2f; }
	float Total = 0.f;
	for (const FName& St : SlotStrain)
	{
		if (const FWeedStrainRow* Row = GetStrainRow(St))
		{
			Total += Row->BaseYieldGrams * CareAvg * SoilMult * PotMult;
		}
	}
	return Total;
}

float AGrowPlant::GetEstimatedThcPercent() const
{
	FSoilDef S; const float SoilMult = GetSoilDef(SoilId, S) ? S.QualityMult : 1.f;
	float Sum = 0.f; int32 N = 0;
	for (const FName& St : SlotStrain)
	{
		if (const FWeedStrainRow* Row = GetStrainRow(St))
		{
			Sum += Row->BaseThcPercent * CareAvg * SoilMult;
			++N;
		}
	}
	return N > 0 ? Sum / N : 0.f;
}

void AGrowPlant::OnRep_Slots()  { UpdatePlantVisual(); }
void AGrowPlant::OnRep_Soil()   { UpdateSoilVisual(); }
void AGrowPlant::OnRep_Pot()    { EnsureSlots(); UpdatePotVisual(); UpdatePlantVisual(); }

void AGrowPlant::UpdatePotVisual()
{
	FPotDef Pot;
	if (Mesh && GetPotDef(PotTier, Pot)) { Mesh->SetRelativeScale3D(Pot.MeshScale); }
}

void AGrowPlant::UpdateSoilVisual()
{
	if (SoilMesh) { SoilMesh->SetVisibility(HasSoil()); }
}

FVector AGrowPlant::SlotLocalOffset(int32 Slot) const
{
	const int32 N = SlotStrain.Num();
	FPotDef Pot;
	const float PotRadius = (GetPotDef(PotTier, Pot)) ? (50.f * Pot.MeshScale.X) : 25.f;
	const float Z = 44.f;
	if (N <= 1) { return FVector(0.f, 0.f, Z); }
	const float R = PotRadius * 0.5f;
	const float Ang = (2.f * PI * Slot) / N;
	return FVector(FMath::Cos(Ang) * R, FMath::Sin(Ang) * R, Z);
}

void AGrowPlant::UpdatePlantVisual()
{
	const int32 N = SlotStrain.Num();
	const float MultiScale = (N <= 1) ? 1.0f : (N <= 2 ? 0.8f : 0.55f);
	for (int32 i = 0; i < PlantMeshes.Num(); ++i)
	{
		UStaticMeshComponent* PM = PlantMeshes[i];
		if (!PM) { continue; }
		const bool bShow = (i < N) && !SlotStrain[i].IsNone();
		PM->SetVisibility(bShow);
		if (!bShow) { continue; }

		FVector Scale;
		switch (SlotPhase[i])
		{
		case EGrowthPhase::Seedling:   Scale = FVector(0.18f, 0.18f, 0.12f); break;
		case EGrowthPhase::Vegetative: Scale = FVector(0.26f, 0.26f, 0.32f); break;
		case EGrowthPhase::PreFlower:  Scale = FVector(0.34f, 0.34f, 0.50f); break;
		case EGrowthPhase::Flower:     Scale = FVector(0.42f, 0.42f, 0.64f); break;
		default:                       Scale = FVector(0.48f, 0.48f, 0.74f); break;
		}
		PM->SetRelativeScale3D(Scale * MultiScale);
		PM->SetRelativeLocation(SlotLocalOffset(i));
		UMaterialInterface* Mat = (SlotPhase[i] == EGrowthPhase::Harvestable && PlantReadyMat) ? PlantReadyMat : PlantMat;
		if (Mat) { PM->SetMaterial(0, Mat); }
	}
}
