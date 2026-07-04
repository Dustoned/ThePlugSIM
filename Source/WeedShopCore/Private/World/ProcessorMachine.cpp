#include "World/ProcessorMachine.h"

#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Inventory/InventoryComponent.h"
#include "Game/WeedShopGameState.h"
#include "Progression/GoalsComponent.h"
#include "Placement/PlaceableProp.h"
#include "World/WorldItemPickup.h"
#include "UI/WeedToast.h"
#include "GameFramework/Pawn.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"

// Statische registry van alle levende machines (zie GetAll in de header): gevuld in BeginPlay,
// geleegd in EndPlay. De upgrade-scan loopt hierdoor O(machines/props) i.p.v. over alle actors.
static TArray<TWeakObjectPtr<AProcessorMachine>> GProcessorRegistry;
const TArray<TWeakObjectPtr<AProcessorMachine>>& AProcessorMachine::GetAll() { return GProcessorRegistry; }

AProcessorMachine::AProcessorMachine()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	SetRootComponent(Mesh);
	if (UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")))
	{
		Mesh->SetStaticMesh(Cube);
	}
	Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);

	Deco = CreateDefaultSubobject<USceneComponent>(TEXT("Deco"));
	Deco->SetupAttachment(Mesh);
}

bool AProcessorMachine::IsPressTier(FName Tier)
{
	return Tier.ToString().StartsWith(TEXT("Press_"));
}

FString AProcessorMachine::InputPrefixFor(FName Tier)
{
	const FString T = Tier.ToString();
	if (T.StartsWith(TEXT("Press_")))  { return TEXT("Crystal_"); }
	if (T.StartsWith(TEXT("Oven_")))   { return TEXT("Bud_"); }       // gedroogde wiet -> bakken (decarb)
	if (T.StartsWith(TEXT("Pan_")))    { return TEXT("Baked_"); }     // gebakken wiet -> boter koken
	if (T.StartsWith(TEXT("Fridge_"))) { return TEXT("ButterMix_"); } // gekookte boter -> laten zetten
	return TEXT("Bud_"); // Mesh_
}

FString AProcessorMachine::OutputPrefixFor(FName Tier)
{
	const FString T = Tier.ToString();
	if (T.StartsWith(TEXT("Press_")))  { return TEXT("Hash_"); }
	if (T.StartsWith(TEXT("Oven_")))   { return TEXT("Baked_"); }
	if (T.StartsWith(TEXT("Pan_")))    { return TEXT("ButterMix_"); }
	if (T.StartsWith(TEXT("Fridge_"))) { return TEXT("Edible_"); }    // eindproduct: cannabutter/edible, hoge THC
	if (T.StartsWith(TEXT("Rosin_")))  { return TEXT("Rosin_"); }     // solventless rosin
	if (T.StartsWith(TEXT("Iso_")))    { return TEXT("Bubble_"); }    // bubble/ice hash (isolator)
	if (T.StartsWith(TEXT("Oil_")))    { return TEXT("Oil_"); }       // cannabis-olie (coating voor moonrocks)
	if (T.StartsWith(TEXT("Moon_")))   { return TEXT("Moonrock_"); }  // moonrocks (gecoate bud)
	return TEXT("Crystal_"); // Mesh_
}

bool AProcessorMachine::GetProcDef(FName Tier, int32& OutCapacity, float& OutSeconds, float& OutConv, float& OutThcMult, bool& bOutIsPress)
{
	const FString T = Tier.ToString();
	bOutIsPress = T.StartsWith(TEXT("Press_"));
	// Mesh: gedroogde wiet -> crystals (lage opbrengst in gram, sterke THC-boost).
	// THC-boost bewust laag gehouden: 90% hasj is de HARDSTE mijlpaal -> alleen met pro-mesh + pro-press (x2.86)
	// EN een Cali-strain (33%+, perfecte zorg). Lagere strains/keten halen de 90%-cap NIET.
	if (T == TEXT("Mesh_Cheap")) { OutCapacity = 1; OutSeconds = 60.f; OutConv = 0.15f; OutThcMult = 1.80f; return true; }
	if (T == TEXT("Mesh_Std"))   { OutCapacity = 2; OutSeconds = 45.f; OutConv = 0.20f; OutThcMult = 2.00f; return true; }
	if (T == TEXT("Mesh_Pro"))   { OutCapacity = 3; OutSeconds = 30.f; OutConv = 0.28f; OutThcMult = 2.20f; return true; }
	// Press: crystals -> hasj (weinig verlies, extra THC-boost).
	if (T == TEXT("Press_Cheap")) { OutCapacity = 1; OutSeconds = 90.f; OutConv = 0.60f; OutThcMult = 1.10f; return true; }
	if (T == TEXT("Press_Std"))   { OutCapacity = 2; OutSeconds = 70.f; OutConv = 0.70f; OutThcMult = 1.20f; return true; }
	if (T == TEXT("Press_Pro"))   { OutCapacity = 3; OutSeconds = 50.f; OutConv = 0.85f; OutThcMult = 1.30f; return true; }
	// Edibles-keten: oven (decarb, klein verlies, lichte THC-activatie) -> pan (boter koken) -> koelkast
	// (laten zetten, grootste THC-boost + cap = "koelkast-voorraad").
	// Edibles schalen multiplicatief mee met de bud-THC die je erin stopt (oven x1.15 -> pan x1.25 -> koelkast
	// x1.55 = x2.23 totaal). Betere weed = duidelijk betere edible; toppt rond ~82% (Cali), nét onder hasj (90%).
	if (T == TEXT("Oven_Std"))    { OutCapacity = 2; OutSeconds = 40.f;  OutConv = 0.92f; OutThcMult = 1.15f; return true; }
	if (T == TEXT("Pan_Std"))     { OutCapacity = 2; OutSeconds = 55.f;  OutConv = 0.88f; OutThcMult = 1.25f; return true; }
	if (T == TEXT("Fridge_Std"))  { OutCapacity = 4; OutSeconds = 180.f; OutConv = 1.00f; OutThcMult = 1.55f; return true; }
	// Pro-edibles: sneller + grotere capaciteit + iets minder verlies (zelfde THC -> hasj blijft de 90%-top).
	if (T == TEXT("Oven_Pro"))    { OutCapacity = 4; OutSeconds = 25.f;  OutConv = 0.96f; OutThcMult = 1.15f; return true; }
	if (T == TEXT("Pan_Pro"))     { OutCapacity = 4; OutSeconds = 35.f;  OutConv = 0.94f; OutThcMult = 1.25f; return true; }
	if (T == TEXT("Fridge_Pro"))  { OutCapacity = 8; OutSeconds = 110.f; OutConv = 1.00f; OutThcMult = 1.55f; return true; }
	// Concentraat-ketens (gedroogde wiet -> concentraat), realistisch oplopend en ALLE onder de 90%-hasj-top.
	// Volgorde Moon -> Rosin -> Isolator (met top-Cali bud ~40%): Moonrock ~52-56% (bud + kief/olie, hoge
	// gram-opbrengst), Rosin ~74-78% (solventless pers), Bubble/Ice "Isolator" ~82-88% (water-extractie, top).
	// Olie-pers: gedroogde wiet -> cannabis-olie (potent concentraat). Tussenproduct: de olie coat de bud in de
	// moonrock-station (zoals boter voor de pan). Niet los aan klanten verkocht.
	if (T == TEXT("Oil_Std"))   { OutCapacity = 1; OutSeconds = 80.f; OutConv = 0.16f; OutThcMult = 1.90f; bOutIsPress = true; return true; }
	if (T == TEXT("Oil_Pro"))   { OutCapacity = 2; OutSeconds = 60.f; OutConv = 0.22f; OutThcMult = 2.00f; bOutIsPress = true; return true; }
	if (T == TEXT("Moon_Std"))  { OutCapacity = 2; OutSeconds = 60.f; OutConv = 0.70f; OutThcMult = 1.30f; bOutIsPress = true; return true; }
	if (T == TEXT("Moon_Pro"))  { OutCapacity = 4; OutSeconds = 45.f; OutConv = 0.80f; OutThcMult = 1.40f; bOutIsPress = true; return true; }
	if (T == TEXT("Rosin_Std")) { OutCapacity = 1; OutSeconds = 75.f; OutConv = 0.18f; OutThcMult = 1.85f; bOutIsPress = true; return true; }
	if (T == TEXT("Rosin_Pro")) { OutCapacity = 2; OutSeconds = 55.f; OutConv = 0.24f; OutThcMult = 1.95f; bOutIsPress = true; return true; }
	if (T == TEXT("Iso_Std"))   { OutCapacity = 1; OutSeconds = 95.f; OutConv = 0.14f; OutThcMult = 2.05f; return true; }
	if (T == TEXT("Iso_Pro"))   { OutCapacity = 2; OutSeconds = 70.f; OutConv = 0.20f; OutThcMult = 2.20f; return true; }
	OutCapacity = 1; OutSeconds = 60.f; OutConv = 0.15f; OutThcMult = 2.0f; return false;
}

int32 AProcessorMachine::Capacity() const { int32 C=1; float S,Cv,M; bool P; GetProcDef(MachineTier, C, S, Cv, M, P); return C; }
float AProcessorMachine::ProcSeconds() const { int32 C; float S=60.f,Cv,M; bool P; GetProcDef(MachineTier, C, S, Cv, M, P); return S * UpSpeedMult; }

void AProcessorMachine::RecomputeUpgrades(float DeltaSeconds)
{
	UpScanTimer -= DeltaSeconds;
	if (UpScanTimer > 0.f) { return; }
	UpScanTimer = 0.5f;
	UWorld* W = GetWorld();
	if (!W) { return; }
	const FVector C = GetActorLocation();
	// Een upgrade hoort bij de DICHTSTBIJZIJNDE machine (zoals pot-gear).
	// PERF: registry-loops (O(machines/props)) i.p.v. TActorIterator over alle actors, en alloc-vrije
	// FName-vergelijking op de 2 bekende ProcUp-ids i.p.v. ItemId.ToString()+StartsWith per prop.
	// De registry is per-proces, dus wél op wereld filteren (PIE/co-op-in-1-proces = meerdere werelden).
	static const FName MotorId(TEXT("ProcUp_Motor")), YieldId(TEXT("ProcUp_Yield")), PurityId(TEXT("ProcUp_Purity"));
	auto NearestProc = [W](const FVector& At) -> AProcessorMachine*
	{
		AProcessorMachine* Best = nullptr; float BestSq = TNumericLimits<float>::Max();
		for (const TWeakObjectPtr<AProcessorMachine>& WM : AProcessorMachine::GetAll()) { AProcessorMachine* P = WM.Get(); if (!IsValid(P) || P->GetWorld() != W) { continue; } const float d = FVector::DistSquared2D(P->GetActorLocation(), At); if (d < BestSq) { BestSq = d; Best = P; } }
		return Best;
	};
	float Speed = 1.f, Yield = 1.f, Quality = 1.f;
	for (const TWeakObjectPtr<APlaceableProp>& WProp : APlaceableProp::GetAll())
	{
		APlaceableProp* P = WProp.Get(); if (!IsValid(P) || P->GetWorld() != W) { continue; }
		const FName Id = P->ItemId;
		if (Id != MotorId && Id != YieldId && Id != PurityId) { continue; }
		const FVector L = P->GetActorLocation();
		if (FVector::Dist2D(L, C) > 175.f || FMath::Abs(L.Z - C.Z) > 280.f) { continue; }
		if (NearestProc(L) != this) { continue; }
		if (Id == MotorId) { Speed *= 0.7f; }
		else if (Id == YieldId) { Yield *= 1.3f; }
		else { Quality *= 1.20f; } // ProcUp_Purity: zuiverder output = hogere kwaliteit -> verkoopt beter
	}
	UpSpeedMult = Speed; UpYieldMult = Yield; UpQualityMult = Quality;
}

void AProcessorMachine::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AProcessorMachine, MachineTier);
	DOREPLIFETIME(AProcessorMachine, Entries);
	DOREPLIFETIME(AProcessorMachine, RepBusy);
	DOREPLIFETIME(AProcessorMachine, RepReady);
	DOREPLIFETIME(AProcessorMachine, RepCapacity);
}

void AProcessorMachine::BeginPlay()
{
	Super::BeginPlay();
	GProcessorRegistry.Add(this);
	// Upgrade-scan-fase random spreiden (0..0,5s): niet alle machines scannen in HETZELFDE frame
	// (zelfde 0,5s-cadans, zelfde uitkomst - alleen de piek verdeeld over frames).
	UpScanTimer = FMath::FRandRange(0.f, 0.5f);
	SetupVisual();
	if (HasAuthority()) { UpdateRep(); }
}

void AProcessorMachine::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GProcessorRegistry.Remove(this);
	Super::EndPlay(EndPlayReason);
}

void AProcessorMachine::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	SetupVisual();
}

void AProcessorMachine::OnRep_Tier() { SetupVisual(); }

void AProcessorMachine::SetupVisual()
{
	if (!Mesh) { return; }
	const bool bPress = IsPressTier(MachineTier);

	// Basis-kast (root), gecentreerd op de actor-origin. Plaatsing tilt 'm met ZOff op de vloer.
	Mesh->SetRelativeScale3D(FVector(0.7f, 0.7f, 0.55f)); // 70 x 70 x 55 cm
	if (UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial")))
	{
		if (UMaterialInstanceDynamic* M = Mesh->CreateDynamicMaterialInstance(0, Base))
		{
			M->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.10f, 0.11f, 0.13f)); // donker metaal
		}
	}

	// Oude deco opruimen.
	for (UStaticMeshComponent* P : Parts) { if (P) { P->DestroyComponent(); } }
	Parts.Reset();

	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	UMaterialInterface* BaseMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	// LocalCm = offset (cm) t.o.v. het body-midden; SizeCm = echte afmeting. Parent-scale 0.7/0.7/0.55 compenseren.
	auto AddPart = [&](const FVector& LocalCm, const FVector& SizeCm, const FLinearColor& Col)
	{
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
		C->SetupAttachment(Mesh);
		C->RegisterComponent();
		if (Cube) { C->SetStaticMesh(Cube); }
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetRelativeScale3D(FVector(SizeCm.X / 70.f, SizeCm.Y / 70.f, SizeCm.Z / 55.f));
		C->SetRelativeLocation(FVector(LocalCm.X / 0.7f, LocalCm.Y / 0.7f, LocalCm.Z / 0.55f));
		if (BaseMat) { if (UMaterialInstanceDynamic* M = C->CreateDynamicMaterialInstance(0, BaseMat)) { M->SetVectorParameterValue(TEXT("Color"), Col); } }
		Parts.Add(C);
	};

	// Body-top zit op LocalCm.Z = +27.5 (halve hoogte). Accenten daarboven.
	const FString TS = MachineTier.ToString();
	const bool bOven = TS.StartsWith(TEXT("Oven_"));
	const bool bPan = TS.StartsWith(TEXT("Pan_"));
	const bool bFridge = TS.StartsWith(TEXT("Fridge_"));
	const FLinearColor Accent = bPress ? FLinearColor(0.95f, 0.5f, 0.15f) : FLinearColor(0.25f, 0.7f, 1.f);
	if (bOven)
	{
		// Oven/fornuis: kookplaat met gloeiende ring + ovendeur-raampje + knoppen.
		Mesh->SetRelativeScale3D(FVector(0.75f, 0.7f, 0.55f));
		AddPart(FVector(0.f, 0.f, 30.f), FVector(72.f, 70.f, 6.f), FLinearColor(0.14f, 0.14f, 0.16f)); // kookplaat
		AddPart(FVector(-16.f, -14.f, 34.f), FVector(20.f, 20.f, 2.f), FLinearColor(0.9f, 0.35f, 0.1f)); // gloeiring
		AddPart(FVector(16.f, 14.f, 34.f), FVector(20.f, 20.f, 2.f), FLinearColor(0.35f, 0.18f, 0.12f));  // 2e pit
		AddPart(FVector(0.f, -34.f, 0.f), FVector(50.f, 3.f, 34.f), FLinearColor(0.5f, 0.55f, 0.6f));  // ovendeur (glanzend)
		AddPart(FVector(0.f, -35.f, 0.f), FVector(34.f, 2.f, 18.f), FLinearColor(0.8f, 0.4f, 0.12f));     // oven-raampje (gloed)
		AddPart(FVector(-26.f, -35.f, 22.f), FVector(5.f, 4.f, 5.f), FLinearColor(0.2f, 0.2f, 0.22f));    // knop
		AddPart(FVector(26.f, -35.f, 22.f), FVector(5.f, 4.f, 5.f), FLinearColor(0.2f, 0.2f, 0.22f));     // knop
	}
	else if (bPan)
	{
		// Kookplaat met een pan (cilinder) + steel.
		AddPart(FVector(0.f, 0.f, 30.f), FVector(70.f, 70.f, 6.f), FLinearColor(0.14f, 0.14f, 0.16f));     // werkblad
		AddPart(FVector(0.f, 0.f, 33.f), FVector(20.f, 20.f, 2.f), FLinearColor(0.85f, 0.35f, 0.12f));     // pit (gloed)
		if (UStaticMesh* Cyl = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder")))
		{
			UStaticMeshComponent* Pan = NewObject<UStaticMeshComponent>(this); Pan->SetupAttachment(Mesh); Pan->RegisterComponent();
			Pan->SetStaticMesh(Cyl); Pan->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Pan->SetRelativeScale3D(FVector(44.f / 70.f, 44.f / 70.f, 9.f / 55.f));
			Pan->SetRelativeLocation(FVector(0.f, 0.f, 38.f / 0.55f));
			if (BaseMat) { if (UMaterialInstanceDynamic* M = Pan->CreateDynamicMaterialInstance(0, BaseMat)) { M->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.05f, 0.05f, 0.06f)); } }
			Parts.Add(Pan);
		}
		AddPart(FVector(0.f, 0.f, 40.f), FVector(34.f, 34.f, 4.f), FLinearColor(0.85f, 0.78f, 0.45f)); // boter/wiet-mix in de pan
		AddPart(FVector(40.f, 0.f, 38.f), FVector(26.f, 5.f, 4.f), FLinearColor(0.1f, 0.1f, 0.11f));   // steel
	}
	else if (bFridge)
	{
		// Koelkast: hoge witte kast met deur-naad, verticale handgreep + vriesvak-lijn.
		if (UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial")))
		{ if (UMaterialInstanceDynamic* M = Mesh->CreateDynamicMaterialInstance(0, Base)) { M->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.82f, 0.83f, 0.85f)); } } // wit
		AddPart(FVector(34.f, -24.f, 0.f), FVector(3.f, 4.f, 70.f), FLinearColor(0.3f, 0.32f, 0.35f));   // handgreep
		AddPart(FVector(35.f, 0.f, 18.f), FVector(2.f, 68.f, 3.f), FLinearColor(0.55f, 0.56f, 0.58f));   // vriesvak-naad
		AddPart(FVector(35.f, 0.f, 0.f), FVector(2.f, 68.f, 2.f), FLinearColor(0.6f, 0.85f, 0.9f));   // koel-display-streepje
	}
	else if (bPress)
	{
		AddPart(FVector(0.f, 0.f, 36.f), FVector(70.f, 70.f, 10.f), FLinearColor(0.18f, 0.18f, 0.2f)); // bovenplaat
		AddPart(FVector(0.f, 0.f, 30.f), FVector(58.f, 58.f, 6.f), Accent);                            // hete plaat (gloed)
		AddPart(FVector(30.f, 0.f, 48.f), FVector(8.f, 8.f, 34.f), FLinearColor(0.3f, 0.3f, 0.33f));   // perskolom
	}
	else
	{
		AddPart(FVector(0.f, 0.f, 38.f), FVector(58.f, 58.f, 22.f), Accent);                           // zeef-trommel
		AddPart(FVector(0.f, 0.f, 52.f), FVector(40.f, 40.f, 8.f), FLinearColor(0.16f, 0.17f, 0.2f));  // deksel
	}
}

void AProcessorMachine::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!HasAuthority()) { return; }
	RecomputeUpgrades(DeltaSeconds); // upgrade-gear in de buurt -> snelheid/opbrengst
	const float Total = FMath::Max(1.f, ProcSeconds());
	bool bChanged = false;
	for (FProcEntry& E : Entries)
	{
		if (!E.bDone)
		{
			E.Elapsed += DeltaSeconds;
			if (E.Elapsed >= Total) { E.bDone = true; bChanged = true; }
		}
	}
	if (bChanged) { UpdateRep(); }
}

int32 AProcessorMachine::ServerLoad(FName InId, int32 Qty, float Thc, float QualPct, const FString& OutPrefixOverride)
{
	if (!HasAuthority() || Qty <= 0) { return 0; }
	int32 Cap = 1; float Sec, Conv, ThcMult; bool bPress;
	if (!GetProcDef(MachineTier, Cap, Sec, Conv, ThcMult, bPress)) { return 0; }
	if (Entries.Num() >= Cap) { return 0; } // vol
	const FString S = InId.ToString();
	const FString Pre = InputPrefixFor(MachineTier);
	if (!S.StartsWith(Pre)) { return 0; }
	const FString Strain = S.RightChop(Pre.Len());

	FProcEntry E;
	const FString OutPre = OutPrefixOverride.IsEmpty() ? OutputPrefixFor(MachineTier) : OutPrefixOverride;
	E.OutItemId = FName(*(OutPre + Strain));
	E.Quantity = FMath::Max(1, FMath::RoundToInt(Qty * Conv * UpYieldMult)); // upgrade-gear verhoogt de opbrengst
	E.Thc = FMath::Min(90.f, (Thc > 0.f ? Thc : 15.f) * ThcMult);
	E.Quality = FMath::Min(100.f, (QualPct > 0.f ? QualPct : 60.f) * UpQualityMult); // Purity-gear verhoogt de kwaliteit
	E.Elapsed = 0.f; E.bDone = false;
	Entries.Add(E);
	UpdateRep();
	return Qty;
}

bool AProcessorMachine::ServerCollectIndex(int32 Index, FName& OutId, int32& OutQty, float& OutThc, float& OutQual)
{
	if (!HasAuthority() || !Entries.IsValidIndex(Index) || !Entries[Index].bDone) { return false; }
	const FProcEntry& E = Entries[Index];
	OutId = E.OutItemId; OutQty = E.Quantity; OutThc = E.Thc; OutQual = E.Quality;
	Entries.RemoveAt(Index);
	UpdateRep();
	return true;
}

void AProcessorMachine::Interact_Implementation(APawn* InstigatorPawn)
{
	if (!HasAuthority() || !InstigatorPawn) { return; }
	UInventoryComponent* Inv = InstigatorPawn->FindComponentByClass<UInventoryComponent>();
	if (!Inv) { return; }

	// 1) Klare batch? -> oogst de eerste klare naar je inventory.
	for (int32 i = 0; i < Entries.Num(); ++i)
	{
		if (Entries[i].bDone)
		{
			FName OutId; int32 OQ; float OT, OQual;
			if (ServerCollectIndex(i, OutId, OQ, OT, OQual))
			{
				// De entry is hierboven al uit de machine: bij een volle inventory mag de batch niet stil
				// verdwijnen -> GiveOrDrop dropt 'm dan bij de voeten.
				AWorldItemPickup::GiveOrDrop(Inv, InstigatorPawn, OutId, OQ, OT, OQual);
				if (AWeedShopGameState* GS = GetWorld() ? GetWorld()->GetGameState<AWeedShopGameState>() : nullptr)
				{ if (UGoalsComponent* Gl = GS->GetGoals()) { Gl->NoteCrafted(OQ); } } // goal-teller: gram gecraft in de lab/keuken
				if (GEngine) { UWeedToast::NotifyPawn(InstigatorPawn, -1, 2.5f, FColor(150, 220, 160), FString::Printf(TEXT("Collected %dg (%.0f%% THC)"), OQ, OT)); }
			}
			return;
		}
	}

	// 2) Anders: laad het hand-item als 't het juiste invoer-type is.
	const FName Act = Inv->GetActiveItemId();
	const FString Pre = InputPrefixFor(MachineTier);
	if (!Act.IsNone() && Act.ToString().StartsWith(Pre))
	{
		if (Entries.Num() >= Capacity())
		{
			if (GEngine) { UWeedToast::NotifyPawn(InstigatorPawn, -1, 2.5f, FColor::Orange, TEXT("Machine is full - collect a batch first.")); }
			return;
		}
		// Laad precies de VASTGEHOUDEN stapel met z'n eigen THC%/kwaliteit (niet alle stapels van die strain).
		const int32 Sid = Inv->GetActiveStackId();
		const int32 Idx = Inv->FindStackById(Sid);
		const TArray<FInventoryStack>& St = Inv->GetStacks();
		if (!St.IsValidIndex(Idx)) { return; }
		const int32 Qty = St[Idx].Quantity;
		if (Qty <= 0) { return; }
		const float Thc = St[Idx].Quality;       // 'Quality'-veld = THC%
		const float Qual = St[Idx].QualityPct;   // kwaliteit%
		// De pan kookt met boter: je hebt minimaal 1 boter nodig (water is gratis), die wordt verbruikt.
		const bool bNeedsButter = MachineTier.ToString().StartsWith(TEXT("Pan_"));
		if (bNeedsButter && !Inv->HasItem(FName(TEXT("Butter")), 1))
		{
			if (GEngine) { UWeedToast::NotifyPawn(InstigatorPawn, -1, 3.f, FColor::Orange, TEXT("Need butter to cook (buy it in the Lab).")); }
			return;
		}
		// Het moonrock-station coat de bud met cannabis-olie: minimaal 1 olie (van de olie-pers) nodig, wordt verbruikt.
		const bool bNeedsOil = MachineTier.ToString().StartsWith(TEXT("Moon_"));
		FName OilId = NAME_None;
		if (bNeedsOil)
		{
			for (const FInventoryStack& OS : Inv->GetStacks())
			{
				if (OS.Quantity > 0 && OS.ItemId.ToString().StartsWith(TEXT("Oil_"))) { OilId = OS.ItemId; break; }
			}
			if (OilId.IsNone())
			{
				if (GEngine) { UWeedToast::NotifyPawn(InstigatorPawn, -1, 3.f, FColor::Orange, TEXT("Need cannabis oil to coat (make it with an oil press).")); }
				return;
			}
		}
		// De koelkast-keuken maakt COOKIES of GUMMIES als je de bak-ingredienten bij je hebt; anders de
		// standaard cannabutter-edible. Sugar is voor beide nodig; + flour -> cookies, + gelatin -> gummies.
		// (Heb je zowel flour als gelatin: cookies krijgen voorrang.) Ingredienten worden verbruikt.
		const bool bFridge = MachineTier.ToString().StartsWith(TEXT("Fridge_"));
		FString OutOverride;
		bool bUseFlour = false, bUseGelatin = false, bUseSugar = false;
		if (bFridge)
		{
			const bool bHasSugar   = Inv->HasItem(FName(TEXT("Sugar")), 1);
			const bool bHasFlour   = Inv->HasItem(FName(TEXT("Flour")), 1);
			const bool bHasGelatin = Inv->HasItem(FName(TEXT("Gelatin")), 1);
			if (bHasSugar && bHasFlour)        { OutOverride = TEXT("Cookie_"); bUseFlour = true; bUseSugar = true; }
			else if (bHasSugar && bHasGelatin) { OutOverride = TEXT("Gummy_");  bUseGelatin = true; bUseSugar = true; }
		}

		const int32 Used = ServerLoad(Act, Qty, Thc, Qual, OutOverride);
		if (Used > 0)
		{
			Inv->RemoveFromStackById(Sid, Used);
			if (bNeedsButter) { Inv->RemoveItem(FName(TEXT("Butter")), 1); }
			if (bNeedsOil && !OilId.IsNone()) { Inv->RemoveItem(OilId, 1); }
			if (bUseFlour)   { Inv->RemoveItem(FName(TEXT("Flour")), 1); }
			if (bUseGelatin) { Inv->RemoveItem(FName(TEXT("Gelatin")), 1); }
			if (bUseSugar)   { Inv->RemoveItem(FName(TEXT("Sugar")), 1); }
			if (GEngine)
			{
				if (!OutOverride.IsEmpty())
				{
					const TCHAR* What = (OutOverride == TEXT("Cookie_")) ? TEXT("Baking cookies") : TEXT("Setting gummies");
					UWeedToast::NotifyPawn(InstigatorPawn, -1, 2.5f, FColor(120, 200, 255), FString::Printf(TEXT("%s (%dg)..."), What, Used));
				}
				else
				{
					UWeedToast::NotifyPawn(InstigatorPawn, -1, 2.5f, FColor(120, 200, 255), FString::Printf(TEXT("Loaded %dg - processing..."), Used));
				}
			}
		}
	}
	else if (GEngine)
	{
		const FString T = MachineTier.ToString();
		FString Msg = TEXT("Hold dried weed (E) to extract crystals.");
		if      (T.StartsWith(TEXT("Oven_")))   { Msg = TEXT("Hold dried weed (E) to bake/decarb it."); }
		else if (T.StartsWith(TEXT("Pan_")))    { Msg = TEXT("Hold baked weed (E) to cook with butter."); }
		else if (T.StartsWith(TEXT("Fridge_"))) { Msg = TEXT("Hold cooked butter (E): + flour & sugar = cookies, + gelatin & sugar = gummies, else edibles."); }
		else if (T.StartsWith(TEXT("Oil_")))    { Msg = TEXT("Hold dried weed (E) to press into cannabis oil."); }
		else if (T.StartsWith(TEXT("Moon_")))   { Msg = TEXT("Hold dried weed (E) to coat with oil into moonrocks."); }
		else if (T.StartsWith(TEXT("Rosin_")))  { Msg = TEXT("Hold dried weed (E) to press into rosin."); }
		else if (T.StartsWith(TEXT("Iso_")))    { Msg = TEXT("Hold dried weed (E) to wash into bubble hash."); }
		else if (IsPressTier(MachineTier))      { Msg = TEXT("Hold crystals (E) to press into hash."); }
		UWeedToast::NotifyPawn(InstigatorPawn, -1, 3.f, FColor::Orange, Msg);
	}
}

FText AProcessorMachine::GetInteractionPrompt_Implementation() const
{
	const FString T = MachineTier.ToString();
	const bool bOven = T.StartsWith(TEXT("Oven_")), bPan = T.StartsWith(TEXT("Pan_")), bFridge = T.StartsWith(TEXT("Fridge_")), bPress = IsPressTier(MachineTier);
	auto CollectLbl = [&]() { return bFridge ? TEXT("Collect edibles") : bPan ? TEXT("Collect cooked butter") : bOven ? TEXT("Collect baked weed") : bPress ? TEXT("Collect hash") : TEXT("Collect crystals"); };
	auto IdleLbl = [&]() { return bFridge ? TEXT("Fridge  (butter mix -> edibles / cookies / gummies)") : bPan ? TEXT("Pan  (cook baked weed + butter)") : bOven ? TEXT("Oven  (bake dried weed)") : bPress ? TEXT("Heatpress  (hold crystals)") : TEXT("Mesh extractor  (hold dried weed)"); };
	// Klaar?
	for (const FProcEntry& E : Entries) { if (E.bDone) { return FText::FromString(CollectLbl()); } }
	// Bezig? -> resterende tijd van de bijna-klare batch.
	float BestLeft = -1.f;
	const float Total = ProcSeconds();
	for (const FProcEntry& E : Entries) { if (!E.bDone) { const float L = Total - E.Elapsed; if (BestLeft < 0.f || L < BestLeft) { BestLeft = L; } } }
	if (BestLeft >= 0.f)
	{
		const int32 S = FMath::CeilToInt(BestLeft);
		const TCHAR* Verb = bFridge ? TEXT("Setting") : (bOven || bPan) ? TEXT("Cooking") : TEXT("Processing");
		return FText::FromString(FString::Printf(TEXT("%s... %d:%02d  (%d/%d)"), Verb, S / 60, S % 60, Entries.Num(), Capacity()));
	}
	return FText::FromString(IdleLbl());
}

void AProcessorMachine::UpdateRep()
{
	int32 B = 0, R = 0;
	for (const FProcEntry& E : Entries) { if (E.bDone) { ++R; } else { ++B; } }
	RepBusy = B; RepReady = R; RepCapacity = Capacity();
}
