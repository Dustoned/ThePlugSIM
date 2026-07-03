#include "World/StorageShelf.h"

#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "Placement/PlaceableTypes.h"
#include "Placement/PropMeshKit.h"
#include "Engine/StaticMesh.h"
#include "Engine/Engine.h"
#include "Net/UnrealNetwork.h"

AStorageShelf::AStorageShelf()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	SetRootComponent(Mesh);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> MatFinder(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (CubeFinder.Succeeded()) { Mesh->SetStaticMesh(CubeFinder.Object); }
	Mesh->SetWorldScale3D(FVector(1.5f, 0.4f, 1.7f)); // exacte schaal komt uit de tier-def
	Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	if (MatFinder.Succeeded())
	{
		DynMat = Mesh->CreateDynamicMaterialInstance(0, MatFinder.Object);
		if (DynMat) { DynMat->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.45f, 0.30f, 0.18f)); } // warm hout
	}

	Deco = PropKit::MakeDeco(this, Mesh, TEXT("Deco"));
	for (int32 i = 0; i < 9; ++i) { Parts.Add(PropKit::MakePart(this, Deco, *FString::Printf(TEXT("Part%d"), i))); }
}

int32 AStorageShelf::GetCapacity() const
{
	if (ShelfTier == FName(TEXT("Chest")))        { return 20; }
	if (ShelfTier == FName(TEXT("Fridge")))       { return 8;  } // basis-koelkast: klein, vroeg in 't spel
	if (ShelfTier == FName(TEXT("Fridge_Large"))) { return 16; } // grotere koelkast (upgrade)
	if (ShelfTier == FName(TEXT("Fridge_XL")))    { return 28; } // walk-in koelkast (top-upgrade)
	if (ShelfTier == FName(TEXT("Safe_Small")))  { return 4;  } // kluis-tiers: item-slots per grootte (gehalveerd, top iets steiler)
	if (ShelfTier == FName(TEXT("Safe_Medium"))) { return 8;  }
	if (ShelfTier == FName(TEXT("Safe_Large")))  { return 14; }
	if (ShelfTier == FName(TEXT("Safe_Vault")))  { return 20; }
	return 24;
}

FString AStorageShelf::GetTitle() const
{
	FPlaceableDef Def;
	if (GetPlaceableDef(ShelfTier, Def)) { return Def.DisplayName.ToUpper(); }
	return TEXT("STORAGE");
}

void AStorageShelf::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AStorageShelf, ShelfTier);
	DOREPLIFETIME(AStorageShelf, Contents);
	DOREPLIFETIME(AStorageShelf, Cooking);
}

void AStorageShelf::SetupVisual()
{
	FPlaceableDef Def;
	if (!Mesh || !GetPlaceableDef(ShelfTier, Def)) { return; }
	Mesh->SetWorldScale3D(Def.MeshScale);

	const bool bChest = (ShelfTier == FName(TEXT("Chest")));
	const bool bFridge = ShelfTier.ToString().StartsWith(TEXT("Fridge")); // alle koelkast-tiers (Fridge / _Large / _XL)
	const bool bSafe = IsSafe();
	const FLinearColor Col = bSafe ? FLinearColor(0.20f, 0.21f, 0.24f) // donker gunmetal (kluis)
		: bChest ? FLinearColor(0.32f, 0.20f, 0.10f)
		: bFridge ? FLinearColor(0.85f, 0.86f, 0.88f) // staal-wit
		: FLinearColor(0.45f, 0.30f, 0.18f);
	if (DynMat) { DynMat->SetVectorParameterValue(TEXT("Color"), Col); }
	if (Parts.Num() < 9) { return; }

	const float W = Def.MeshScale.X * 100.f;
	const float D = Def.MeshScale.Y * 100.f;
	const float H = Def.MeshScale.Z * 100.f;
	const float Floor = -H * 0.5f;
	const FLinearColor Dark = Col * 0.7f;
	const FLinearColor Light = Col * 1.25f;

	Mesh->SetVisibility(false);

	if (bFridge)
	{
		// Koelkast: romp + horizontale naad (vriesvak/koelvak) + verticale handgreep.
		const FLinearColor Steel(0.78f, 0.80f, 0.83f), HandleC(0.25f, 0.26f, 0.28f), Seam(0.6f, 0.62f, 0.65f);
		PropKit::SetPart(Parts[0], PropKit::Cube(), FVector(W, D, H), FVector(0, 0, 0), Steel);                                  // romp
		PropKit::SetPart(Parts[1], PropKit::Cube(), FVector(W * 1.01f, D * 0.04f, H * 0.02f), FVector(0, -D * 0.5f, Floor + H * 0.66f), Seam); // deur-naad
		PropKit::SetPart(Parts[2], PropKit::Cube(), FVector(W * 0.06f, D * 0.06f, H * 0.28f), FVector(W * 0.32f, -D * 0.5f - 2.f, Floor + H * 0.78f), HandleC); // greep boven
		PropKit::SetPart(Parts[3], PropKit::Cube(), FVector(W * 0.06f, D * 0.06f, H * 0.40f), FVector(W * 0.32f, -D * 0.5f - 2.f, Floor + H * 0.30f), HandleC); // greep onder
		for (int32 i = 4; i < 9; ++i) { if (Parts[i]) { Parts[i]->SetVisibility(false); } }
	}
	else if (bChest)
	{
		// Kist: romp + schuin deksel + 2 sloten/beslag.
		const float BodyH = H * 0.62f;
		PropKit::SetPart(Parts[0], PropKit::Cube(), FVector(W, D, BodyH), FVector(0, 0, Floor + BodyH * 0.5f), Col);
		PropKit::SetPart(Parts[1], PropKit::Cube(), FVector(W * 1.02f, D * 1.02f, H * 0.30f), FVector(0, 0, Floor + BodyH + H * 0.13f), Light, FRotator(8.f, 0.f, 0.f));
		PropKit::SetPart(Parts[2], PropKit::Cube(), FVector(W * 0.10f, D * 1.04f, BodyH * 0.5f), FVector(-W * 0.30f, 0, Floor + BodyH * 0.5f), Dark);
		PropKit::SetPart(Parts[3], PropKit::Cube(), FVector(W * 0.10f, D * 1.04f, BodyH * 0.5f), FVector( W * 0.30f, 0, Floor + BodyH * 0.5f), Dark);
		PropKit::SetPart(Parts[4], PropKit::Cube(), FVector(W * 0.14f, D * 0.10f, H * 0.10f), FVector(0, D * 0.5f, Floor + BodyH * 0.55f), FLinearColor(0.7f, 0.6f, 0.2f));
		for (int32 i = 5; i < 9; ++i) { if (Parts[i]) { Parts[i]->SetVisibility(false); } }
	}
	else if (bSafe)
	{
		// Kluis: solide romp + deur-paneel + draaiknop + greep (dicht, niet open zoals een schap).
		const FLinearColor Door(0.28f, 0.29f, 0.33f), Dial(0.55f, 0.56f, 0.60f), HandleC(0.6f, 0.6f, 0.62f);
		PropKit::SetPart(Parts[0], PropKit::Cube(), FVector(W, D, H), FVector(0, 0, 0), Col);
		PropKit::SetPart(Parts[1], PropKit::Cube(), FVector(W * 0.80f, D * 0.06f, H * 0.80f), FVector(0, -D * 0.5f, 0), Door);
		PropKit::SetPart(Parts[2], PropKit::Cube(), FVector(W * 0.16f, D * 0.10f, H * 0.16f), FVector(0, -D * 0.5f - 2.f, 0), Dial);
		PropKit::SetPart(Parts[3], PropKit::Cube(), FVector(W * 0.05f, D * 0.10f, H * 0.34f), FVector(W * 0.28f, -D * 0.5f - 2.f, 0), HandleC);
		for (int32 i = 4; i < 9; ++i) { if (Parts[i]) { Parts[i]->SetVisibility(false); } }
	}
	else
	{
		// Schap: 2 zijpanelen + achterwand + 4 legborden.
		const float PanelT = FMath::Min(5.f, W * 0.06f);
		PropKit::SetPart(Parts[0], PropKit::Cube(), FVector(PanelT, D, H), FVector(-W * 0.5f + PanelT * 0.5f, 0, 0), Col);
		PropKit::SetPart(Parts[1], PropKit::Cube(), FVector(PanelT, D, H), FVector( W * 0.5f - PanelT * 0.5f, 0, 0), Col);
		PropKit::SetPart(Parts[2], PropKit::Cube(), FVector(W, PanelT, H), FVector(0, D * 0.5f - PanelT * 0.5f, 0), Dark);
		// 4 planken verdeeld over de hoogte.
		for (int32 s = 0; s < 4; ++s)
		{
			const float Z = Floor + H * (0.10f + 0.27f * s);
			PropKit::SetPart(Parts[3 + s], PropKit::Cube(), FVector(W - PanelT * 2.f, D * 0.92f, PanelT), FVector(0, 0, Z), Light);
		}
		for (int32 i = 7; i < 9; ++i) { if (Parts[i]) { Parts[i]->SetVisibility(false); } }
	}
}

void AStorageShelf::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	SetupVisual();
}

void AStorageShelf::OnRep_Tier()
{
	SetupVisual();
}

void AStorageShelf::BeginPlay()
{
	Super::BeginPlay();
	SetupVisual();
	// Versheid: een GEWONE plank/kist laat boter/edibles ook bederven (alleen een Fridge koelt).
	// Server-timer elke 10s; Fridge-shelves slaan we over zodat ze de inhoud vers houden.
	if (HasAuthority() && !ShelfTier.ToString().StartsWith(TEXT("Fridge")) && !IsSafe())
	{
		GetWorldTimerManager().SetTimer(PerishTimer, this, &AStorageShelf::DegradeShelfPerishables, 10.f, true, 10.f);
	}
	// Koelkast: 1s-timer die lopende edible-batches laat zetten en het resultaat in de voorraad legt.
	if (HasAuthority() && IsFridge())
	{
		GetWorldTimerManager().SetTimer(CookTimer, this, &AStorageShelf::TickCooking, 1.f, true, 1.f);
	}
}

void AStorageShelf::TickCooking()
{
	if (!HasAuthority() || Cooking.Num() == 0) { return; }
	int32 Cap; float Sec = 180.f, Conv, Mult; bool bP;
	AProcessorMachine::GetProcDef(TEXT("Fridge_Std"), Cap, Sec, Conv, Mult, bP);
	const float Total = FMath::Max(1.f, Sec);
	bool bChanged = false;
	for (int32 i = Cooking.Num() - 1; i >= 0; --i)
	{
		FProcEntry& E = Cooking[i];
		if (!E.bDone)
		{
			E.Elapsed += 1.f;
			if (E.Elapsed >= Total) { E.bDone = true; }
			bChanged = true;
		}
		if (E.bDone)
		{
			// Klaar -> in de koelkast-voorraad. Lukt 't niet (koelkast vol), volgende tick opnieuw proberen.
			if (ServerStore(E.OutItemId, E.Quantity, E.Thc, E.Quality) > 0) { Cooking.RemoveAt(i); bChanged = true; }
		}
	}
	if (bChanged) { ForceNetUpdate(); }
}

bool AStorageShelf::ServerStartEdible(const FString& Strain, int32 Qty, float Thc, float Qual, const FString& OutPrefix)
{
	if (!HasAuthority() || !IsFridge() || Qty <= 0 || Strain.IsEmpty()) { return false; }
	if (Cooking.Num() >= FridgeCookCap()) { return false; }
	int32 Cap; float Sec, Conv = 1.f, Mult = 1.55f; bool bP;
	AProcessorMachine::GetProcDef(TEXT("Fridge_Std"), Cap, Sec, Conv, Mult, bP); // hergebruik 't koelkast-recept
	FProcEntry E;
	const FString Pre = OutPrefix.IsEmpty() ? TEXT("Edible_") : OutPrefix;
	E.OutItemId = FName(*(Pre + Strain));
	E.Quantity = FMath::Max(1, FMath::RoundToInt(Qty * Conv));
	E.Thc = FMath::Min(90.f, (Thc > 0.f ? Thc : 15.f) * Mult);
	E.Quality = Qual > 0.f ? Qual : 60.f;
	E.Elapsed = 0.f; E.bDone = false;
	Cooking.Add(E);
	ForceNetUpdate();
	return true;
}

void AStorageShelf::DegradeShelfPerishables()
{
	const float Step = 1.6f;
	bool bChanged = false;
	for (FShelfStack& S : Contents)
	{
		const FString Id = S.ItemId.ToString();
		const bool bPerish = Id.StartsWith(TEXT("ButterMix")) || Id.StartsWith(TEXT("Edible")) || Id == TEXT("Butter")
			|| Id.StartsWith(TEXT("Cookie")) || Id.StartsWith(TEXT("Gummy"));
		if (bPerish && S.QualityPct > 0.f)
		{
			S.QualityPct = FMath::Max(0.f, S.QualityPct - Step);
			bChanged = true;
		}
	}
	if (bChanged) { ForceNetUpdate(); }
}

int32 AStorageShelf::ServerStore(FName ItemId, int32 Count, float Thc, float QualityPct)
{
	if (!HasAuthority() || ItemId.IsNone() || Count <= 0) { return 0; }

	// Zelfde item + THC + kwaliteit -> samenvoegen op een bestaande stapel.
	for (FShelfStack& S : Contents)
	{
		if (S.ItemId == ItemId && FMath::IsNearlyEqual(S.Thc, Thc, 0.5f) && FMath::IsNearlyEqual(S.QualityPct, QualityPct, 0.5f))
		{
			S.Quantity += Count;
			return Count;
		}
	}
	// Anders een nieuwe stapel (als er ruimte is).
	if (Contents.Num() >= GetCapacity()) { return 0; }
	FShelfStack NewS;
	NewS.ItemId = ItemId; NewS.Quantity = Count; NewS.Thc = Thc; NewS.QualityPct = QualityPct;
	Contents.Add(NewS);
	return Count;
}

int32 AStorageShelf::ServerTake(int32 SlotIndex, int32 Count, FName ExpectedId, FName& OutId, float& OutThc, float& OutQualityPct)
{
	if (!HasAuthority() || !Contents.IsValidIndex(SlotIndex) || Count <= 0) { return 0; }
	// CO-OP anti-race: als een ANDERE speler tussentijds een eerder slot verwijderde (RemoveAt schuift de indices
	// op), wijst deze client-index nu een ander item aan -> weiger i.p.v. het verkeerde item (evt. Cash) te pakken.
	if (!ExpectedId.IsNone() && Contents[SlotIndex].ItemId != ExpectedId) { return 0; }
	FShelfStack& S = Contents[SlotIndex];
	const int32 Taken = FMath::Min(Count, S.Quantity);
	OutId = S.ItemId; OutThc = S.Thc; OutQualityPct = S.QualityPct;
	S.Quantity -= Taken;
	if (S.Quantity <= 0) { Contents.RemoveAt(SlotIndex); }
	return Taken;
}

void AStorageShelf::Interact_Implementation(APawn* InstigatorPawn)
{
	// Het schap-menu openen gebeurt lokaal in de character (UI-actie); hier niets te doen.
}

FText AStorageShelf::GetInteractionPrompt_Implementation() const
{
	const bool bChest = (ShelfTier == FName(TEXT("Chest")));
	const bool bFridge = ShelfTier.ToString().StartsWith(TEXT("Fridge"));
	const TCHAR* Name = IsSafe() ? TEXT("Safe") : (bChest ? TEXT("Storage chest") : (bFridge ? TEXT("Fridge") : TEXT("Storage shelf")));
	return FText::FromString(FString::Printf(TEXT("%s  (%d/%d slots)"), Name, Contents.Num(), GetCapacity()));
}
