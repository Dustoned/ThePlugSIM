#include "Cultivation/GrowPlant.h"
#include "UI/WeedToast.h"

#include "WeedShopCore.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Data/WeedStrain.h"
#include "Cultivation/SoilTypes.h"
#include "Cultivation/PotTypes.h"
#include "Cultivation/WaterCanComponent.h"
#include "Inventory/InventoryComponent.h"
#include "Game/WeedShopGameState.h"
#include "Progression/UpgradeComponent.h"
#include "Progression/StoreComponent.h"
#include "Progression/LevelComponent.h"
#include "Progression/GoalsComponent.h"
#include "Engine/Engine.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"
#include "EngineUtils.h"
#include "Placement/PlaceableProp.h"

// Aantal bladeren en toppen per plant (samengestelde plant-look).
static constexpr int32 FoliagePerPlant = 6;
static constexpr int32 BudsPerPlant = 4;

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

	// Rand-lip (boven) + voetje (onder): geven de kale cilinder een echte bloempot-vorm.
	PotRim = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PotRim"));
	PotRim->SetupAttachment(Root);
	PotRim->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PotFoot = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PotFoot"));
	PotFoot->SetupAttachment(Root);
	PotFoot->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	if (PotMeshFinder.Succeeded())
	{
		PotRim->SetStaticMesh(PotMeshFinder.Object);
		PotFoot->SetStaticMesh(PotMeshFinder.Object);
	}

	// Donkere binnenkant (altijd zichtbaar): een verzonken schijf die de pot 'leeg/hol' laat ogen.
	PotInner = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PotInner"));
	PotInner->SetupAttachment(Root);
	PotInner->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	if (PotMeshFinder.Succeeded())
	{
		PotInner->SetStaticMesh(PotMeshFinder.Object);
		PotInner->SetRelativeScale3D(FVector(0.43f, 0.43f, 0.06f)); // dunne verzonken bodem
		PotInner->SetRelativeLocation(FVector(0.f, 0.f, 33.f));     // net onder de rand
	}

	SoilMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SoilMesh"));
	SoilMesh->SetupAttachment(Root);
	SoilMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	if (PotMeshFinder.Succeeded())
	{
		SoilMesh->SetStaticMesh(PotMeshFinder.Object);
		// Duidelijke bruine vulling die de donkere bodem bedekt en tot net onder de rand komt.
		SoilMesh->SetRelativeScale3D(FVector(0.43f, 0.43f, 0.20f));
		SoilMesh->SetRelativeLocation(FVector(0.f, 0.f, 40.f));
	}
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> SoilMatFinder(TEXT("/Game/_Project/Materials/M_Soil.M_Soil"));
	if (SoilMatFinder.Succeeded()) { SoilMesh->SetMaterial(0, SoilMatFinder.Object); }
	SoilMesh->SetVisibility(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylForPlant(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> ConeForPlant(TEXT("/Engine/BasicShapes/Cone.Cone"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BudBasicMat(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> PlantMatFinder(TEXT("/Game/_Project/Materials/M_Plant.M_Plant"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> PlantReadyFinder(TEXT("/Game/_Project/Materials/M_PlantReady.M_PlantReady"));
	if (PlantMatFinder.Succeeded()) { PlantMat = PlantMatFinder.Object; }
	if (PlantReadyFinder.Succeeded()) { PlantReadyMat = PlantReadyFinder.Object; }

	// Samengestelde plant per plek: steel (cilinder) + bossige blad-clusters + toppen (bollen).
	auto MakePlantPart = [&](USceneComponent* Parent, const TCHAR* Name, UStaticMesh* M) -> UStaticMeshComponent*
	{
		UStaticMeshComponent* C = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		C->SetupAttachment(Parent);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		if (M) { C->SetStaticMesh(M); }
		if (PlantMat) { C->SetMaterial(0, PlantMat); }
		C->SetVisibility(false);
		return C;
	};
	for (int32 i = 0; i < MaxSlots; ++i)
	{
		USceneComponent* PR = CreateDefaultSubobject<USceneComponent>(*FString::Printf(TEXT("PlantRoot%d"), i));
		PR->SetupAttachment(Root);
		PlantRoots.Add(PR);

		PlantStems.Add(MakePlantPart(PR, *FString::Printf(TEXT("Stem%d"), i), CylForPlant.Succeeded() ? CylForPlant.Object : nullptr));
		for (int32 k = 0; k < FoliagePerPlant; ++k)
		{
			PlantLeaves.Add(MakePlantPart(PR, *FString::Printf(TEXT("Leaf%d_%d"), i, k), ConeForPlant.Succeeded() ? ConeForPlant.Object : nullptr));
		}
		for (int32 b = 0; b < BudsPerPlant; ++b)
		{
			UStaticMeshComponent* Bud = MakePlantPart(PR, *FString::Printf(TEXT("Bud%d_%d"), i, b), ConeForPlant.Succeeded() ? ConeForPlant.Object : nullptr);
			// Buds krijgen een eigen KLEURBAAR materiaal (groen tijdens bloei, donkerpaars als 'ie klaar is).
			if (Bud && BudBasicMat.Succeeded()) { Bud->CreateDynamicMaterialInstance(0, BudBasicMat.Object); }
			PlantBuds.Add(Bud);
		}
	}

	// Ziek-markers (zwevend bolletje per plek): wit = mold, oranje = pest.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereFinder(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BasicMatFinder(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	for (int32 i = 0; i < MaxSlots; ++i)
	{
		UStaticMeshComponent* MK = CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("Sick%d"), i));
		MK->SetupAttachment(Root);
		MK->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		if (SphereFinder.Succeeded()) { MK->SetStaticMesh(SphereFinder.Object); }
		MK->SetRelativeScale3D(FVector(0.12f));
		if (BasicMatFinder.Succeeded()) { MK->CreateDynamicMaterialInstance(0, BasicMatFinder.Object); }
		MK->SetVisibility(false);
		SickMarkers.Add(MK);
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

	// Donkere binnenkant -> de lege pot oogt hol; soil (bruin) bedekt 'm zodra je 'm toevoegt.
	if (PotInner && !InnerMID)
	{
		if (UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial")))
		{
			InnerMID = PotInner->CreateDynamicMaterialInstance(0, Base);
			if (InnerMID) { InnerMID->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.04f, 0.04f, 0.05f)); }
		}
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
	Out.SlotAfflict = SlotAfflict;
	Out.SlotAfflictTime = SlotAfflictTime;
	Out.FertYieldMult = FertYieldMult;
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

	FertYieldMult = (In.FertYieldMult > 0.f) ? In.FertYieldMult : 1.f;
	const int32 N = SlotStrain.Num();
	for (int32 i = 0; i < N; ++i)
	{
		SlotStrain[i] = In.SlotStrain.IsValidIndex(i) ? In.SlotStrain[i] : NAME_None;
		SlotGrowth[i] = In.SlotGrowth.IsValidIndex(i) ? In.SlotGrowth[i] : 0.f;
		SlotPhase[i] = In.SlotPhase.IsValidIndex(i) ? (EGrowthPhase)In.SlotPhase[i] : EGrowthPhase::Seedling;
		SlotAfflict[i] = In.SlotAfflict.IsValidIndex(i) ? In.SlotAfflict[i] : 0;
		SlotAfflictTime[i] = In.SlotAfflictTime.IsValidIndex(i) ? In.SlotAfflictTime[i] : 0.f;
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
	DOREPLIFETIME(AGrowPlant, SlotAfflict);
	DOREPLIFETIME(AGrowPlant, SlotAfflictTime);
	DOREPLIFETIME(AGrowPlant, FertYieldMult);
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
		SlotAfflict.Init(0, N);
		SlotAfflictTime.Init(0.f, N);
	}
	// Zorg dat de mold-arrays altijd even lang zijn (oude saves).
	if (SlotAfflict.Num() != SlotStrain.Num()) { SlotAfflict.Init(0, SlotStrain.Num()); }
	if (SlotAfflictTime.Num() != SlotStrain.Num()) { SlotAfflictTime.Init(0.f, SlotStrain.Num()); }
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

FString AGrowPlant::GetActiveUpgradesLabel() const
{
	const TArray<FPotUpgradeDef>& Ups = GetPotUpgrades();
	TArray<FString> Names;
	for (int32 i = 0; i < Ups.Num(); ++i)
	{
		if (HasPotUpgrade(i) && Ups.IsValidIndex(i)) { Names.Add(Ups[i].DisplayName); }
	}
	return FString::Join(Names, TEXT(", "));
}

void AGrowPlant::RecomputeGearUpgradeMask(float DeltaSeconds)
{
	GearScanTimer -= DeltaSeconds;
	if (GearScanTimer > 0.f) { return; }
	GearScanTimer = 0.5f; // niet elke tick scannen

	UWorld* W = GetWorld();
	if (!W) { return; }
	const FVector C = GetActorLocation();
	const int32 PotTierIdx = GetPotTierIndex(PotTier); // -1 = onbekend
	const TArray<FPotUpgradeDef>& Ups = GetPotUpgrades();

	// Nieuwste regel: PER-POT. Een gear telt alleen voor de pot waar 'ie het DICHTST bij staat,
	// niet voor alle potten in de buurt. Helper: de dichtstbijzijnde pot bij een gear-positie.
	auto NearestPot = [W](const FVector& At) -> AGrowPlant*
	{
		AGrowPlant* Best = nullptr; float BestSq = TNumericLimits<float>::Max();
		for (TActorIterator<AGrowPlant> P(W); P; ++P)
		{
			if (!IsValid(*P)) { continue; }
			const float dSq = FVector::DistSquared2D(P->GetActorLocation(), At);
			if (dSq < BestSq) { BestSq = dSq; Best = *P; }
		}
		return Best;
	};

	int32 Mask = 0;
	for (TActorIterator<APlaceableProp> It(W); It; ++It)
	{
		APlaceableProp* P = *It;
		if (!IsValid(P)) { continue; }
		const int32 Ui = GearUpgradeIndex(P->ItemId);
		if (Ui < 0) { continue; }
		const FVector L = P->GetActorLocation();
		// Moet binnen bereik van DEZE pot staan...
		if (FVector::Dist2D(L, C) > 175.f || FMath::Abs(L.Z - C.Z) > 280.f) { continue; }
		// ...en deze pot moet de dichtstbijzijnde pot bij de gear zijn (anders hoort 'ie bij de buur-pot).
		if (NearestPot(L) != this) { continue; }
		// Tier-gating: gear werkt alleen op een pot van de vereiste tier of hoger.
		if (Ups.IsValidIndex(Ui) && PotTierIdx < Ups[Ui].MinPotTierIndex) { continue; }
		Mask |= (1 << Ui);
	}
	PotUpgradeMask = Mask; // afgeleid van de fysieke gear; per-pot gekoppeld
}

void AGrowPlant::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (HasAuthority())
	{
		RecomputeGearUpgradeMask(DeltaSeconds); // pot-gear bepaalt nu de bonussen (geen abstracte upgrades meer)
	}
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

	// Groei per plek (grow-lamp I/II/III = +15/30/50% sneller).
	const int32 LampTier = HighestOwnedTier(PotUpgradeMask, { 3, 4, 5 });
	const float LampMul = 1.f + ((LampTier == 1) ? 0.15f : (LampTier == 2) ? 0.30f : (LampTier >= 3) ? 0.50f : 0.f);
	const float Speed = FMath::Max(0.f, GrowthSpeedMultiplier) * (1.f + GrowthBonus) * LampMul;
	for (int32 i = 0; i < SlotStrain.Num(); ++i)
	{
		// Besmette planten (mold/pest) stoppen met groeien tot je ze sprayt.
		if (SlotStrain[i].IsNone() || SlotPhase[i] == EGrowthPhase::Harvestable) { continue; }
		if (SlotAfflict.IsValidIndex(i) && SlotAfflict[i] != 0) { continue; }
		SlotGrowth[i] = FMath::Min(SlotGrowth[i] + DeltaSeconds * Speed, SlotMaxSeconds(i));
	}
	UpdatePhases();

	// Waterpeil loopt langzaam leeg (isolatie-upgrade + pot-retentie vertragen dit).
	// Auto-watering I/II (bits 9/10) houdt de pot vanzelf vol -> geen handmatig water nodig.
	const float MaxCare = GetMaxCare();
	if (HasPotUpgrade(9) || HasPotUpgrade(10))
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
		// Droog -> zakt verder weg (0.12) en herstelt LANGZAAM (interp 0.12), zodat last-minute water het
		// tijd-gemiddelde nauwelijks meer optrekt: je moet er gedurende de hele groei water in houden.
		const float Target = (WaterLevel >= 0.25f) ? MaxCare : 0.12f;
		CareMultiplier = FMath::Clamp(FMath::FInterpTo(CareMultiplier, Target, DeltaSeconds, 0.12f), 0.05f, MaxCare);

		// Kwaliteit = tijd-gewogen gemiddelde gezondheid.
		CareSum += CareMultiplier * DeltaSeconds;
		CareTime += DeltaSeconds;
		CareAvg = (CareTime > 0.f) ? (CareSum / CareTime) : CareMultiplier;
	}
	else
	{
		// Over-rijp: niet geoogst -> de gezondheid (health) zakt LANGZAAM in ECHTE tijd (niet x groeisnelheid,
		// anders gaat 'ie veel te snel dood). Het bulk-deel (tot 10%) duurt ~4 min, de laatste 10% nog ~3 min;
		// pas daarna sterft de plant. Zo heb je ruim tijd om een klare plant te oogsten.
		const float BulkRate = 0.90f / (240.f * FMath::Max(0.25f, RotBulkFactor * 0.5f)); // ~90% over ~4 min
		const float SlowRate = 0.10f / (180.f * FMath::Max(0.25f, RotSlowFactor * 0.33f)); // laatste 10% over ~3 min
		const float Rate = (CareMultiplier > 0.10f) ? BulkRate : SlowRate;
		CareMultiplier = FMath::Max(0.f, CareMultiplier - DeltaSeconds * Rate);

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
				if (GEngine) { UWeedToast::NotifyPawn(GetOwner(), -1, 4.f, FColor::Red, TEXT("A plant rotted away - harvested too late. Seed lost.")); }
			}
		}
	}

	// --- Mold / pest ---
	// Vanaf een bepaald crew-level kunnen planten schimmel (mold) of ongedierte (pest) krijgen. De kans
	// schaalt met de zorg-kwaliteit (slechter verzorgd = grotere kans). Een besmette plant stopt met
	// groeien; je hebt 3 min om 'm te sprayen, anders gaat 'ie daarna langzaam dood (zaad weg).
	{
		int32 CrewLevel = 1;
		if (const AWeedShopGameState* GS2 = GetWorld() ? GetWorld()->GetGameState<AWeedShopGameState>() : nullptr)
		{
			if (const ULevelComponent* Lv = GS2->GetLeveling()) { CrewLevel = Lv->GetLevel(); }
		}
		constexpr int32 AfflictMoldMinLevel = 12;    // mold (schimmel) kan vanaf hier
		constexpr int32 AfflictPestMinLevel = 18;    // ongedierte (pests) pas later in het spel
		constexpr float AfflictGraceSeconds = 180.f; // 3 min curable (groei gehalt)
		constexpr float AfflictDeathSeconds = 150.f; // daarna langzaam dood
		// Bewust ZELDZAAM: af en toe een dingetje, geen straf op lage kwaliteit. ~1 keer per
		// ~20-30 min groei per plant. Lage care verhoogt het maar mild (max ~1.5x), niet eindeloos.
		constexpr float AfflictBaseRatePerSec = 0.00009f;

		bool bVisChanged = false;
		for (int32 i = 0; i < SlotStrain.Num(); ++i)
		{
			if (SlotStrain[i].IsNone())
			{
				if (SlotAfflict[i] != 0) { SlotAfflict[i] = 0; SlotAfflictTime[i] = 0.f; }
				continue;
			}
			if (SlotAfflict[i] != 0)
			{
				SlotAfflictTime[i] += DeltaSeconds; // echte tijd, niet x groeisnelheid
				if (SlotAfflictTime[i] >= AfflictGraceSeconds + AfflictDeathSeconds)
				{
					SlotStrain[i] = NAME_None; SlotGrowth[i] = 0.f; SlotPhase[i] = EGrowthPhase::Seedling;
					SlotAfflict[i] = 0; SlotAfflictTime[i] = 0.f; bVisChanged = true;
					if (GEngine) { UWeedToast::NotifyPawn(GetOwner(), -1, 4.f, FColor::Red, TEXT("A plant died from mold/pests - you didn't spray it in time. Seed lost.")); }
				}
			}
			else if (CrewLevel >= AfflictMoldMinLevel)
			{
				// Verzorging beschermt sterker: perfecte zorg 0.5x risico, verwaarloosd ~1.7x. Niet save-bound
				// (pure random per seconde), dus de basiskans is bewust laag gehouden.
				const float Risk = AfflictBaseRatePerSec * (0.5f + 1.2f * (1.f - FMath::Clamp(CareMultiplier, 0.f, 1.f)));
				if (FMath::FRand() < Risk * DeltaSeconds)
				{
					// Mold eerst beschikbaar; pests pas vanaf een hoger level (dan 50/50).
					const bool bPestUnlocked = CrewLevel >= AfflictPestMinLevel;
					SlotAfflict[i] = (bPestUnlocked && FMath::FRand() < 0.5f) ? 2 : 1; // 1 mold, 2 pest
					SlotAfflictTime[i] = 0.f; bVisChanged = true;
					if (GEngine) { UWeedToast::NotifyPawn(GetOwner(), -1, 4.f, FColor(255, 140, 40), FString::Printf(TEXT("A plant caught %s! Spray it within 3 min or it starts dying."), SlotAfflict[i] == 1 ? TEXT("MOLD") : TEXT("PESTS"))); }
				}
			}
		}
		if (bVisChanged) { UpdatePlantVisual(); }
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

	// 0) Spray in de hand -> behandel zieke planten (gaat vóór oogsten, zodat je klare zieke planten kunt redden).
	if (Hand.ToString().StartsWith(TEXT("Spray_")))
	{
		TryApplySpray(InstigatorPawn, Hand);
		return;
	}
	// 0b) Mest in de hand -> opbrengst-boost voor deze cyclus.
	if (Hand.ToString().StartsWith(TEXT("Fertilizer_")))
	{
		TryApplyFertilizer(InstigatorPawn, Hand);
		return;
	}

	// 1) Oogstklare planten? Oogst alles tegelijk (zieke planten worden overgeslagen -> eerst sprayen).
	if (GetReadyCount() > 0)
	{
		HarvestReady(InstigatorPawn);
		return;
	}
	// 2) Geen soil? Soil in de hand toevoegen.
	if (!HasSoil())
	{
		if (IsSoilItem(Hand)) { TryAddSoil(InstigatorPawn, Hand); }
		else if (GEngine) { UWeedToast::NotifyPawn(InstigatorPawn,-1, 2.5f, FColor::Orange, TEXT("Hold soil in your hand (1-8) to fill the pot.")); }
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
		UWeedToast::NotifyPawn(InstigatorPawn,-1, 2.5f, FColor::Orange, Msg);
	}
}

int32 AGrowPlant::GetAfflictedCount() const
{
	int32 N = 0;
	for (int32 i = 0; i < SlotAfflict.Num(); ++i) { if (SlotAfflict[i] != 0) { ++N; } }
	return N;
}

float AGrowPlant::GetWorstAfflictSecondsLeft() const
{
	constexpr float Total = 180.f + 150.f;
	float Worst = -1.f;
	for (int32 i = 0; i < SlotAfflict.Num(); ++i)
	{
		if (SlotAfflict[i] == 0) { continue; }
		const float Left = FMath::Max(0.f, Total - (SlotAfflictTime.IsValidIndex(i) ? SlotAfflictTime[i] : 0.f));
		if (Worst < 0.f || Left < Worst) { Worst = Left; }
	}
	return Worst < 0.f ? 0.f : Worst;
}

bool AGrowPlant::TryApplySpray(APawn* InstigatorPawn, FName SprayItem)
{
	UInventoryComponent* Inv = InstigatorPawn ? InstigatorPawn->FindComponentByClass<UInventoryComponent>() : nullptr;
	if (!Inv) { return false; }
	const FString S = SprayItem.ToString();
	// Welke besmettingen behandelt deze spray? Fungicide=mold(1), Pesticide=pest(2), Broad=beide.
	const bool bCureMold = S.Contains(TEXT("Fungicide")) || S.Contains(TEXT("Broad"));
	const bool bCurePest = S.Contains(TEXT("Pesticide")) || S.Contains(TEXT("Pest")) || S.Contains(TEXT("Broad"));

	int32 Cured = 0;
	for (int32 i = 0; i < SlotAfflict.Num(); ++i)
	{
		if ((SlotAfflict[i] == 1 && bCureMold) || (SlotAfflict[i] == 2 && bCurePest))
		{
			SlotAfflict[i] = 0; SlotAfflictTime[i] = 0.f; ++Cured;
		}
	}
	if (Cured == 0)
	{
		if (GEngine) { UWeedToast::NotifyPawn(InstigatorPawn,-1, 2.5f, FColor::Orange, GetAfflictedCount() > 0 ? TEXT("Wrong spray for this problem.") : TEXT("Nothing to treat here.")); }
		return false;
	}
	Inv->RemoveItem(SprayItem, 1); // 1 spray per behandeling
	UpdatePlantVisual();
	if (GEngine) { UWeedToast::NotifyPawn(InstigatorPawn,-1, 3.f, FColor::Green, FString::Printf(TEXT("Treated %d plant(s). They'll resume growing."), Cured)); }
	return true;
}

bool AGrowPlant::TryApplyFertilizer(APawn* InstigatorPawn, FName FertItem)
{
	UInventoryComponent* Inv = InstigatorPawn ? InstigatorPawn->FindComponentByClass<UInventoryComponent>() : nullptr;
	if (!Inv) { return false; }
	if (GetPlantedCount() == 0)
	{
		if (GEngine) { UWeedToast::NotifyPawn(InstigatorPawn,-1, 2.5f, FColor::Orange, TEXT("Plant something first before fertilizing.")); }
		return false;
	}
	const FString S = FertItem.ToString();
	const float Bonus = S.Contains(TEXT("Bloom")) ? 1.30f : 1.15f; // bloom-mest sterker
	if (FertYieldMult >= Bonus - 0.001f)
	{
		if (GEngine) { UWeedToast::NotifyPawn(InstigatorPawn,-1, 2.5f, FColor::Orange, TEXT("This pot is already fertilized.")); }
		return false;
	}
	if (!Inv->RemoveItem(FertItem, 1)) { return false; }
	FertYieldMult = Bonus;
	if (GEngine) { UWeedToast::NotifyPawn(InstigatorPawn,-1, 3.f, FColor::Green, FString::Printf(TEXT("Fertilized: +%.0f%% yield this harvest."), (Bonus - 1.f) * 100.f)); }
	return true;
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
		UWeedToast::NotifyPawn(InstigatorPawn,-1, 3.f, FColor::Green,
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
		UWeedToast::NotifyPawn(InstigatorPawn,-1, 3.f, FColor::Green,
			FString::Printf(TEXT("Planted: %s  (%d/%d)"), *StrainToPlant.ToString(), GetPlantedCount(), GetNumSlots()));
	}
	return true;
}

void AGrowPlant::WaterAll(APawn* InstigatorPawn)
{
	if (GetPlantedCount() == 0)
	{
		if (GEngine) { UWeedToast::NotifyPawn(InstigatorPawn,-1, 2.f, FColor::Orange, TEXT("Nothing planted to water yet.")); }
		return;
	}
	UWaterCanComponent* Can = InstigatorPawn ? InstigatorPawn->FindComponentByClass<UWaterCanComponent>() : nullptr;
	if (!Can || !Can->HasBottle())
	{
		if (GEngine) { UWeedToast::NotifyPawn(InstigatorPawn,-1, 2.5f, FColor::Orange, TEXT("You need a water bottle (buy one from the supplier).")); }
		return;
	}
	if (!Can->TryUseCharge())
	{
		if (GEngine) { UWeedToast::NotifyPawn(InstigatorPawn,-1, 2.5f, FColor::Orange, TEXT("Water bottle is empty - fill it at the sink.")); }
		return;
	}
	WaterLevel = FMath::Clamp(WaterLevel + 0.6f, 0.f, 1.f);
	if (GEngine)
	{
		UWeedToast::NotifyPawn(InstigatorPawn,-1, 2.f, FColor::Cyan,
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
	if (HasPotUpgrade(2)) { PotYield *= 1.2f; }   // bloom booster
	if (HasPotUpgrade(10)) { PotYield *= 1.1f; }  // auto-water II nutrient dosing

	int32 Harvested = 0, TotalGrams = 0, SkippedSick = 0;
	for (int32 i = 0; i < SlotStrain.Num(); ++i)
	{
		if (SlotStrain[i].IsNone() || SlotPhase[i] != EGrowthPhase::Harvestable) { continue; }
		// Zieke (mold/pest) planten kun je niet oogsten -> eerst sprayen.
		if (SlotAfflict.IsValidIndex(i) && SlotAfflict[i] != 0) { ++SkippedSick; continue; }
		const FWeedStrainRow* Row = GetStrainRow(SlotStrain[i]);
		if (!Row || Row->HarvestProductId.IsNone()) { continue; }

		// Kwaliteit (0..1) = verzorging x soil-kwaliteit. Bepaalt zowel opbrengst als THC%.
		const float CareQ = FMath::Clamp(CareAvg, 0.f, 1.f);
		const float QualityFrac = FMath::Clamp(CareQ * SoilQuality, 0.f, 1.f);
		const int32 YieldGrams = FMath::Max(1, FMath::RoundToInt(Row->BaseYieldGrams * CareQ * SoilYield * PotYield * FertYieldMult));

		// THC% afgeleid van strain-potentie x kwaliteit. Wiet heeft ALTIJD THC% (floor), 0% kan niet:
		// slecht verzorgd = gewoon zwakke wiet, geen "no weed". Op hele % afgerond zodat oogsten van
		// dezelfde kwaliteit netjes samen stapelen (en alleen echt andere batches apart blijven).
		// THC-kwaliteit: verzorging + SOIL + POT + FERTILIZER dragen ALLE bij (gewogen). Alles op z'n best +
		// perfecte zorg => je haalt de strain-max (base). Elke upgrade tilt je THC zichtbaar omhoog richting die max.
		const FString SoilS = SoilId.ToString();
		const float SoilQ = SoilS.Contains(TEXT("Premium")) ? 1.0f : SoilS.Contains(TEXT("Rich")) ? 0.75f : 0.5f;
		const FString PotS = PotTier.ToString();
		float PotQ = PotS.Contains(TEXT("Fabric")) ? 0.85f : PotS.Contains(TEXT("Plastic")) ? 0.70f : PotS.Contains(TEXT("Clay")) ? 0.55f : 0.40f;
		if (HasPotUpgrade(0)) { PotQ += 0.04f; }                                                              // drainage: +max quality
		PotQ += HasPotUpgrade(8) ? 0.11f : HasPotUpgrade(7) ? 0.07f : HasPotUpgrade(6) ? 0.04f : 0.f;          // grow tent I/II/III
		PotQ = FMath::Clamp(PotQ, 0.40f, 1.0f);
		const float FertQ = FMath::Clamp(0.6f + (FertYieldMult - 1.0f), 0.6f, 1.0f);
		const float GearQ = FMath::Clamp(0.35f * CareQ + 0.25f * SoilQ + 0.22f * PotQ + 0.18f * FertQ, 0.f, 1.f);
		const float ThcRaw = Row->BaseThcPercent * GearQ * FMath::FRandRange(0.94f, 1.06f);
		// 40% is een ZACHTE grens: met top-setup haal je gemiddeld ~40, maar een gelukkige batch kan er een paar %
		// boven duiken (opscheppen dat je betere wiet maakte dan je co-op vriend). Alleen een ruime veiligheidscap.
		const float ThcPercent = FMath::RoundToFloat(FMath::Min(46.f, FMath::Max(Row->BaseThcPercent * 0.15f, FMath::Max(1.0f, ThcRaw))));
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
			if (UGoalsComponent* Gl = GS->GetGoals()) { Gl->NoteHarvest(Harvested); } // goal-teller: geoogste planten
		}

		// Eén oogst-actie verbruikt één soil-lading + de mest-boost.
		if (SoilUsesLeft > 0) { SoilUsesLeft--; }
		if (SoilUsesLeft <= 0)
		{
			SoilId = NAME_None;
			UpdateSoilVisual();
		}
		FertYieldMult = 1.f; // mest is voor één oogst
		UpdatePlantVisual();
		if (GEngine)
		{
			UWeedToast::NotifyPawn(InstigatorPawn,-1, 4.f, FColor::Green,
				FString::Printf(TEXT("Harvested %d plant(s): %dg total"), Harvested, TotalGrams));
			if (SoilId.IsNone())
			{
				UWeedToast::NotifyPawn(InstigatorPawn,-1, 4.f, FColor::Orange, TEXT("The soil is used up - add fresh soil before replanting."));
			}
		}
	}
	if (SkippedSick > 0 && GEngine)
	{
		UWeedToast::NotifyPawn(InstigatorPawn,-1, 4.f, FColor(255, 140, 40), FString::Printf(TEXT("%d ready plant(s) are sick - spray them before harvest."), SkippedSick));
	}
}

FText AGrowPlant::GetInteractionPrompt_Implementation() const
{
	// Zieke planten hebben voorrang in de prompt: spray ze, met de resterende tijd.
	const int32 Sick = GetAfflictedCount();
	if (Sick > 0)
	{
		// Welk type? (toon mold of pest op basis van de eerste zieke plek)
		bool bMold = false, bPest = false;
		for (int32 i = 0; i < SlotAfflict.Num(); ++i) { if (SlotAfflict[i] == 1) { bMold = true; } else if (SlotAfflict[i] == 2) { bPest = true; } }
		const TCHAR* Kind = (bMold && bPest) ? TEXT("MOLD+PESTS") : (bMold ? TEXT("MOLD") : TEXT("PESTS"));
		const int32 Left = FMath::CeilToInt(GetWorstAfflictSecondsLeft());
		// Clean prompt: type + tijd. Het aantal zieke plekken + uitleg staat op de plant-kaart (HUD).
		return FText::FromString(FString::Printf(TEXT("%s! Spray now  (%d:%02d)"), Kind, Left / 60, Left % 60));
	}
	if (GetReadyCount() > 0)
	{
		// Quality staat op de plant-kaart; hier kort. Wel waarschuwen als 'ie zakt.
		const float Q = FMath::Clamp(CareAvg, 0.f, 1.f) * 100.f;
		const FString Warn = (Q < 60.f) ? TEXT("  - quality dropping!") : TEXT("");
		const int32 Ready = GetReadyCount();
		return FText::FromString(Ready > 1
			? FString::Printf(TEXT("Harvest  (%d ready)%s"), Ready, *Warn)
			: FString::Printf(TEXT("Harvest%s"), *Warn));
	}
	if (!HasSoil())
	{
		return NSLOCTEXT("WeedShop", "AddSoil", "Add soil (hold soil)");
	}
	if (GetPlantedCount() < GetNumSlots())
	{
		return FText::FromString(FString::Printf(TEXT("Plant a seed (hold seed)  (%d/%d)"), GetPlantedCount(), GetNumSlots()));
	}
	return NSLOCTEXT("WeedShop", "WaterPlant", "Water the plant (hold bottle)"); // water-% staat al op de plant-kaart
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
	// Grow-tent I/II/III (bits 6/7/8): hoger kwaliteitsplafond per tier.
	const int32 TentTier = HighestOwnedTier(PotUpgradeMask, { 6, 7, 8 });
	const float Tent = (TentTier == 1) ? 0.08f : (TentTier == 2) ? 0.15f : (TentTier >= 3) ? 0.22f : 0.f;
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
	if (HasPotUpgrade(10)) { PotMult *= 1.1f; }
	PotMult *= FMath::Max(1.f, FertYieldMult); // mest-bonus deze cyclus
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
	// Zelfde weging als bij de oogst: verzorging + soil + pot + fertilizer bepalen hoe dicht je bij de strain-max komt.
	const float CareQ = FMath::Clamp(CareAvg, 0.f, 1.f);
	const FString SoilS = SoilId.ToString();
	const float SoilQ = SoilS.Contains(TEXT("Premium")) ? 1.0f : SoilS.Contains(TEXT("Rich")) ? 0.75f : 0.5f;
	const FString PotS = PotTier.ToString();
	float PotQ = PotS.Contains(TEXT("Fabric")) ? 0.85f : PotS.Contains(TEXT("Plastic")) ? 0.70f : PotS.Contains(TEXT("Clay")) ? 0.55f : 0.40f;
	if (HasPotUpgrade(0)) { PotQ += 0.04f; }
	PotQ += HasPotUpgrade(8) ? 0.11f : HasPotUpgrade(7) ? 0.07f : HasPotUpgrade(6) ? 0.04f : 0.f;
	PotQ = FMath::Clamp(PotQ, 0.40f, 1.0f);
	const float FertQ = FMath::Clamp(0.6f + (FertYieldMult - 1.0f), 0.6f, 1.0f);
	const float GearQ = FMath::Clamp(0.35f * CareQ + 0.25f * SoilQ + 0.22f * PotQ + 0.18f * FertQ, 0.f, 1.f);
	float Sum = 0.f; int32 N = 0;
	for (const FName& St : SlotStrain)
	{
		if (const FWeedStrainRow* Row = GetStrainRow(St))
		{
			Sum += FMath::Min(40.f, Row->BaseThcPercent * GearQ);
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
	if (!Mesh || !GetPotDef(PotTier, Pot)) { return; }

	const FVector S = Pot.MeshScale;
	Mesh->SetRelativeScale3D(S);

	// Top/onderkant van de pot (mesh-pivot staat vast op z=20; cilinder = 100cm hoog).
	const float TopZ = 20.f + S.Z * 50.f;
	const float BotZ = 20.f - S.Z * 50.f;

	// Rand-lip: iets breder dan de pot, dun, net onder de bovenrand -> klassiek bloempot-silhouet.
	if (PotRim)
	{
		PotRim->SetRelativeScale3D(FVector(S.X * 1.14f, S.Y * 1.14f, 0.05f));
		PotRim->SetRelativeLocation(FVector(0.f, 0.f, TopZ - 2.5f));
	}
	// Voetje: smal randje onderaan zodat 'ie niet plat op de grond plakt.
	if (PotFoot)
	{
		PotFoot->SetRelativeScale3D(FVector(S.X * 0.88f, S.Y * 0.88f, 0.06f));
		PotFoot->SetRelativeLocation(FVector(0.f, 0.f, BotZ + 3.f));
	}
	// Donkere holte: de cilinder heeft een DICHTE bovendop, dus een schijf eronder zou onzichtbaar zijn.
	// We leggen een smalle, donkere schijf NET BOVEN de dop en smaller dan de pot, zodat de potwand als
	// rand zichtbaar blijft -> de lege pot oogt hol/leeg van binnen (donker gat in een lichtere rand).
	if (PotInner)
	{
		PotInner->SetRelativeScale3D(FVector(S.X * 0.82f, S.Y * 0.82f, 0.05f));
		PotInner->SetRelativeLocation(FVector(0.f, 0.f, TopZ - 1.0f)); // bovenvlak ~TopZ+1.5, net boven de dop
	}
	// Aarde: bruine schijf NET BOVEN de holte (bedekt het donker) en iets smaller, zodat je 'm duidelijk
	// ziet 'instromen' zodra je soil toevoegt.
	if (SoilMesh)
	{
		SoilMesh->SetRelativeScale3D(FVector(S.X * 0.80f, S.Y * 0.80f, 0.06f));
		SoilMesh->SetRelativeLocation(FVector(0.f, 0.f, TopZ - 0.5f)); // bovenvlak ~TopZ+2.5, dekt de holte af
	}

	// Kleur per tier (gedeeld over pot + rand + voet) zodat het materiaal niet meer kaal grijs is.
	const FString Id = PotTier.ToString();
	FLinearColor Col(0.62f, 0.34f, 0.20f); // terracotta (default)
	if (Id == TEXT("Pot_Broken"))       { Col = FLinearColor(0.50f, 0.36f, 0.28f); } // verweerd
	else if (Id == TEXT("Pot_Clay"))    { Col = FLinearColor(0.66f, 0.33f, 0.18f); } // klei/terracotta
	else if (Id == TEXT("Pot_Plastic")) { Col = FLinearColor(0.16f, 0.17f, 0.19f); } // zwart plastic
	else if (Id == TEXT("Pot_Fabric"))  { Col = FLinearColor(0.46f, 0.42f, 0.34f); } // stoffen kweekzak

	// Basis-materiaal MET een "Color"-parameter. De BasicShapes-cilinder heeft standaard GEEN materiaal
	// (default grid -> "no texture"), dus we maken de dynamic material expliciet van BasicShapeMaterial.
	UMaterialInterface* TintBase = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	auto Tint = [TintBase](UStaticMeshComponent* C, const FLinearColor& Color)
	{
		if (!C) { return; }
		UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(C->GetMaterial(0));
		if (!MID) { MID = C->CreateDynamicMaterialInstance(0, TintBase); }
		if (MID) { MID->SetVectorParameterValue(TEXT("Color"), Color); }
	};
	Tint(Mesh, Col);
	Tint(PotRim, Col * 1.12f);  // rand iets lichter
	Tint(PotFoot, Col * 0.85f); // voet iets donkerder
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
	const float MultiScale = (N <= 1) ? 1.0f : (N <= 2 ? 0.85f : 0.6f);

	for (int32 i = 0; i < PlantRoots.Num(); ++i)
	{
		const bool bShow = (i < N) && SlotStrain.IsValidIndex(i) && !SlotStrain[i].IsNone();
		USceneComponent* PR = PlantRoots[i];
		UStaticMeshComponent* Stem = PlantStems.IsValidIndex(i) ? PlantStems[i] : nullptr;

		auto LeafAt = [&](int32 k) -> UStaticMeshComponent* { const int32 Idx = i * FoliagePerPlant + k; return PlantLeaves.IsValidIndex(Idx) ? PlantLeaves[Idx] : nullptr; };
		auto BudAt  = [&](int32 b) -> UStaticMeshComponent* { const int32 Idx = i * BudsPerPlant + b; return PlantBuds.IsValidIndex(Idx) ? PlantBuds[Idx] : nullptr; };

		if (!bShow)
		{
			if (Stem) { Stem->SetVisibility(false); }
			for (int32 k = 0; k < FoliagePerPlant; ++k) { if (UStaticMeshComponent* L = LeafAt(k)) { L->SetVisibility(false); } }
			for (int32 b = 0; b < BudsPerPlant; ++b) { if (UStaticMeshComponent* Bd = BudAt(b)) { Bd->SetVisibility(false); } }
			continue;
		}

		if (PR) { PR->SetRelativeLocation(SlotLocalOffset(i)); }

		// Groeifase -> hoe ver de plant is (0..1) en welke onderdelen tonen.
		const EGrowthPhase Ph = SlotPhase.IsValidIndex(i) ? SlotPhase[i] : EGrowthPhase::Seedling;
		float F = 0.2f; int32 Leaves = 1; bool bBuds = false; bool bRipe = false;
		switch (Ph)
		{
		case EGrowthPhase::Seedling:   F = 0.22f; Leaves = 1; break;
		case EGrowthPhase::Vegetative: F = 0.50f; Leaves = 2; break;
		case EGrowthPhase::PreFlower:  F = 0.72f; Leaves = 3; bBuds = true; break;
		case EGrowthPhase::Flower:     F = 0.88f; Leaves = 3; bBuds = true; break;
		default:                       F = 1.00f; Leaves = 3; bBuds = true; bRipe = true; break; // Harvestable
		}
		const float Ms = MultiScale;
		const float StemH = FMath::Lerp(10.f, 60.f, F) * Ms;  // steel-hoogte (cm)
		const float StemR = (1.6f + 0.9f * F) * Ms;           // steel-straal (cm)

		// Steel: dunne cilinder vanaf de grond omhoog (groen).
		if (Stem)
		{
			Stem->SetVisibility(true);
			Stem->SetRelativeScale3D(FVector(StemR * 2.f / 100.f, StemR * 2.f / 100.f, StemH / 100.f));
			Stem->SetRelativeLocation(FVector(0.f, 0.f, StemH * 0.5f));
			Stem->SetRelativeRotation(FRotator::ZeroRotator);
			if (PlantMat) { Stem->SetMaterial(0, PlantMat); }
		}

		// Bladeren: spitse kegel-bladeren in kransen langs de steel, naar buiten/omhoog wijzend.
		// Verdeeld over whorls; aantal zichtbaar schaalt met de fase.
		const int32 ShowLeaves = FMath::Clamp(Leaves * 2, 2, FoliagePerPlant);
		for (int32 k = 0; k < FoliagePerPlant; ++k)
		{
			UStaticMeshComponent* L = LeafAt(k);
			if (!L) { continue; }
			const bool bShowLeaf = (k < ShowLeaves);
			L->SetVisibility(bShowLeaf);
			if (!bShowLeaf) { continue; }
			// Whorl-hoogte + hoek rond de steel (verspringend zodat het vol oogt).
			const int32 Whorl = k / 2;             // 0,0,1,1,2,2
			const float HZ = StemH * (0.30f + 0.22f * Whorl);
			const float Ang = (2.f * PI * k) / 2.f + Whorl * 0.8f; // afwisselend links/rechts + offset
			const float LeafLen = FMath::Lerp(12.f, 30.f, F) * Ms;
			// Breed + plat blad (fan-leaf): breed in één as, dun in de andere.
			const float LeafWide = LeafLen * 0.85f;  // breedte
			const float LeafThin = LeafLen * 0.10f;  // dikte
			// Kegel wijst standaard +Z; kantel 'm naar buiten/omhoog (pitch ~ -60°) en draai rond de steel.
			L->SetRelativeRotation(FRotator(-60.f, FMath::RadiansToDegrees(Ang), 0.f));
			L->SetRelativeScale3D(FVector(LeafThin / 100.f, LeafWide / 100.f, LeafLen / 100.f));
			// Iets uit het midden zodat de basis bij de steel zit.
			const float Out = StemR + LeafLen * 0.12f;
			L->SetRelativeLocation(FVector(FMath::Cos(Ang) * Out, FMath::Sin(Ang) * Out, HZ));
			if (PlantMat) { L->SetMaterial(0, PlantMat); }
		}

		// Toppen/colas: rechtopstaande kegels bovenin. Verschijnen vanaf pre-bloei; bij OOGSTKLAAR
		// worden ze fors GROTER + DONKERPAARS zodat je in één oogopslag ziet dat 'ie klaar is.
		const float BudScaleRipe = bRipe ? 1.2f : 0.7f;
		const FLinearColor BudFlower(0.30f, 0.55f, 0.22f); // jong = groen
		const FLinearColor BudRipe(0.26f, 0.08f, 0.34f);   // klaar = donkerpaars
		for (int32 b = 0; b < BudsPerPlant; ++b)
		{
			UStaticMeshComponent* Bd = BudAt(b);
			if (!Bd) { continue; }
			Bd->SetVisibility(bBuds);
			if (!bBuds) { continue; }
			const bool bMain = (b == 0);
			const float BaseLen = FMath::Lerp(5.f, 11.f, F) * Ms;
			const float BudLen = BaseLen * BudScaleRipe * (bMain ? 1.3f : 1.0f);
			const float BudW = BudLen * 0.45f;
			float BX = 0.f, BY = 0.f, BZ = StemH + BudLen * 0.4f; // hoofd-cola bovenop de steel
			if (!bMain)
			{
				const float Ang = (2.f * PI * b) / FMath::Max(1, BudsPerPlant - 1);
				const float Out = (StemR + 6.f * Ms);
				BX = FMath::Cos(Ang) * Out; BY = FMath::Sin(Ang) * Out; BZ = StemH * (0.68f + 0.07f * b);
			}
			Bd->SetRelativeRotation(FRotator::ZeroRotator); // rechtop (kegel wijst omhoog)
			Bd->SetRelativeScale3D(FVector(BudW / 100.f, BudW / 100.f, BudLen / 100.f));
			Bd->SetRelativeLocation(FVector(BX, BY, BZ));
			if (UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(Bd->GetMaterial(0)))
			{
				MID->SetVectorParameterValue(TEXT("Color"), bRipe ? BudRipe : BudFlower);
			}
		}
	}

	// Ziek-markers: zwevend bolletje boven een besmette plant (wit = mold, oranje = pest).
	for (int32 i = 0; i < SickMarkers.Num(); ++i)
	{
		UStaticMeshComponent* MK = SickMarkers[i];
		if (!MK) { continue; }
		const bool bSick = (i < N) && SlotStrain.IsValidIndex(i) && !SlotStrain[i].IsNone()
			&& SlotAfflict.IsValidIndex(i) && SlotAfflict[i] != 0;
		MK->SetVisibility(bSick);
		if (!bSick) { continue; }
		MK->SetRelativeLocation(SlotLocalOffset(i) + FVector(0.f, 0.f, 58.f));
		const FLinearColor C = (SlotAfflict[i] == 1)
			? FLinearColor(0.92f, 0.92f, 0.86f)  // mold = vuilwit/grijs
			: FLinearColor(0.95f, 0.45f, 0.12f); // pest = oranje
		if (UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(MK->GetMaterial(0)))
		{
			MID->SetVectorParameterValue(TEXT("Color"), C);
		}
	}
}
