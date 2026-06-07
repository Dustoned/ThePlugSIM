#include "World/ProcessorMachine.h"

#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Inventory/InventoryComponent.h"
#include "Placement/PlaceableProp.h"
#include "UI/WeedToast.h"
#include "GameFramework/Pawn.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"

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
	return IsPressTier(Tier) ? TEXT("Crystal_") : TEXT("Bud_");
}

FString AProcessorMachine::OutputPrefixFor(FName Tier)
{
	return IsPressTier(Tier) ? TEXT("Hash_") : TEXT("Crystal_");
}

bool AProcessorMachine::GetProcDef(FName Tier, int32& OutCapacity, float& OutSeconds, float& OutConv, float& OutThcMult, bool& bOutIsPress)
{
	const FString T = Tier.ToString();
	bOutIsPress = T.StartsWith(TEXT("Press_"));
	// Mesh: gedroogde wiet -> crystals (lage opbrengst in gram, sterke THC-boost).
	if (T == TEXT("Mesh_Cheap")) { OutCapacity = 1; OutSeconds = 60.f; OutConv = 0.15f; OutThcMult = 2.2f; return true; }
	if (T == TEXT("Mesh_Std"))   { OutCapacity = 2; OutSeconds = 45.f; OutConv = 0.20f; OutThcMult = 2.4f; return true; }
	if (T == TEXT("Mesh_Pro"))   { OutCapacity = 3; OutSeconds = 30.f; OutConv = 0.28f; OutThcMult = 2.6f; return true; }
	// Press: crystals -> hasj (weinig verlies, extra THC-boost).
	if (T == TEXT("Press_Cheap")) { OutCapacity = 1; OutSeconds = 90.f; OutConv = 0.60f; OutThcMult = 1.25f; return true; }
	if (T == TEXT("Press_Std"))   { OutCapacity = 2; OutSeconds = 70.f; OutConv = 0.70f; OutThcMult = 1.30f; return true; }
	if (T == TEXT("Press_Pro"))   { OutCapacity = 3; OutSeconds = 50.f; OutConv = 0.85f; OutThcMult = 1.40f; return true; }
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
	auto NearestProc = [W](const FVector& At) -> AProcessorMachine*
	{
		AProcessorMachine* Best = nullptr; float BestSq = TNumericLimits<float>::Max();
		for (TActorIterator<AProcessorMachine> P(W); P; ++P) { if (!IsValid(*P)) { continue; } const float d = FVector::DistSquared2D(P->GetActorLocation(), At); if (d < BestSq) { BestSq = d; Best = *P; } }
		return Best;
	};
	float Speed = 1.f, Yield = 1.f;
	for (TActorIterator<APlaceableProp> It(W); It; ++It)
	{
		APlaceableProp* P = *It; if (!IsValid(P)) { continue; }
		const FString S = P->ItemId.ToString();
		if (!S.StartsWith(TEXT("ProcUp_"))) { continue; }
		const FVector L = P->GetActorLocation();
		if (FVector::Dist2D(L, C) > 175.f || FMath::Abs(L.Z - C.Z) > 280.f) { continue; }
		if (NearestProc(L) != this) { continue; }
		if (S == TEXT("ProcUp_Motor")) { Speed *= 0.7f; }
		else if (S == TEXT("ProcUp_Yield")) { Yield *= 1.3f; }
	}
	UpSpeedMult = Speed; UpYieldMult = Yield;
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
	SetupVisual();
	if (HasAuthority()) { UpdateRep(); }
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
	const FLinearColor Accent = bPress ? FLinearColor(0.95f, 0.5f, 0.15f) : FLinearColor(0.25f, 0.7f, 1.f);
	if (bPress)
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

int32 AProcessorMachine::ServerLoad(FName InId, int32 Qty, float Thc, float QualPct)
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
	E.OutItemId = FName(*(OutputPrefixFor(MachineTier) + Strain));
	E.Quantity = FMath::Max(1, FMath::RoundToInt(Qty * Conv * UpYieldMult)); // upgrade-gear verhoogt de opbrengst
	E.Thc = FMath::Min(90.f, (Thc > 0.f ? Thc : 15.f) * ThcMult);
	E.Quality = QualPct > 0.f ? QualPct : 60.f;
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
				Inv->AddItem(OutId, OQ, OT, OQual);
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
		const int32 Used = ServerLoad(Act, Qty, Thc, Qual);
		if (Used > 0)
		{
			Inv->RemoveFromStackById(Sid, Used);
			if (GEngine)
			{
				UWeedToast::NotifyPawn(InstigatorPawn, -1, 2.5f, FColor(120, 200, 255), FString::Printf(TEXT("Loaded %dg - processing..."), Used));
			}
		}
	}
	else if (GEngine)
	{
		UWeedToast::NotifyPawn(InstigatorPawn, -1, 3.f, FColor::Orange, IsPressTier(MachineTier)
			? TEXT("Hold crystals (E) to press into hash.")
			: TEXT("Hold dried weed (E) to extract crystals."));
	}
}

FText AProcessorMachine::GetInteractionPrompt_Implementation() const
{
	const bool bPress = IsPressTier(MachineTier);
	// Klaar?
	for (const FProcEntry& E : Entries) { if (E.bDone) { return FText::FromString(bPress ? TEXT("Collect hash (E)") : TEXT("Collect crystals (E)")); } }
	// Bezig? -> resterende tijd van de bijna-klare batch.
	float BestLeft = -1.f;
	const float Total = ProcSeconds();
	for (const FProcEntry& E : Entries) { if (!E.bDone) { const float L = Total - E.Elapsed; if (BestLeft < 0.f || L < BestLeft) { BestLeft = L; } } }
	if (BestLeft >= 0.f)
	{
		const int32 S = FMath::CeilToInt(BestLeft);
		return FText::FromString(FString::Printf(TEXT("Processing... %d:%02d  (%d/%d)"), S / 60, S % 60, Entries.Num(), Capacity()));
	}
	return FText::FromString(bPress ? TEXT("Heatpress - hold crystals + E") : TEXT("Mesh extractor - hold dried weed + E"));
}

void AProcessorMachine::UpdateRep()
{
	int32 B = 0, R = 0;
	for (const FProcEntry& E : Entries) { if (E.bDone) { ++R; } else { ++B; } }
	RepBusy = B; RepReady = R; RepCapacity = Capacity();
}
