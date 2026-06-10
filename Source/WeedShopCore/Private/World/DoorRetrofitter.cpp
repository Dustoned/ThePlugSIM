#include "World/DoorRetrofitter.h"

#include "WeedShopCore.h"
#include "World/CityDoor.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "Components/StaticMeshComponent.h"
#include "EngineUtils.h"
#include "TimerManager.h"

namespace
{
	// Bekende deur-blad-meshes (pivot op het scharnier). L-bladen lopen lokaal -Y vanaf de pivot,
	// R-bladen +Y -> gespiegelde open-draai zodat dubbele deuren netjes dezelfde kant in zwaaien.
	struct FLeafDef { const TCHAR* MeshName; float OpenDeg; };
	const FLeafDef LeafDefs[] = {
		{ TEXT("SM_Base_Door_01_L"), -100.f },
		{ TEXT("SM_Base_Door_01_R"),  100.f },
		{ TEXT("SM_Base_Door_08_L"), -100.f },
		{ TEXT("SM_Base_Door_08_R"),  100.f },
		{ TEXT("SM_Door_Apartment01"), -100.f },                 // appartement-deur
		{ TEXT("SM_Door_Apartment02"), -100.f },                 // appartement-deur (variant)
		{ TEXT("SM_DoorCommerical_01"), -100.f },                // winkeldeur
		{ TEXT("SM_Upper_BalconyDoor_01_3m_Door"), -100.f },     // balkondeur (glas via _Glass)
		{ TEXT("SM_Upper_BalconyDoor_01_3m_Door_b"), -100.f },   // balkondeur variant
	};

	const FLeafDef* MatchLeaf(const UStaticMesh* Mesh)
	{
		if (!Mesh) { return nullptr; }
		const FString Name = Mesh->GetName();
		for (const FLeafDef& D : LeafDefs)
		{
			if (Name == D.MeshName) { return &D; }
		}
		return nullptr;
	}

	// Hoort deze mesh als GLAS bij een van de bekende bladen? (naam == "<blad>_Glass")
	bool IsLeafGlass(const UStaticMesh* Mesh)
	{
		if (!Mesh) { return false; }
		const FString Name = Mesh->GetName();
		for (const FLeafDef& D : LeafDefs)
		{
			if (Name == FString(D.MeshName) + TEXT("_Glass")) { return true; }
		}
		return false;
	}
}

ADoorRetrofitter::ADoorRetrofitter()
{
	PrimaryActorTick.bCanEverTick = false;
}

void ADoorRetrofitter::BeginPlay()
{
	Super::BeginPlay();
	// Meteen een eerste pass + daarna periodiek blijven scannen (world partition / level instances
	// streamen gebouwen later in; die deuren pakken we dan alsnog).
	ScanAndConvert();
	GetWorldTimerManager().SetTimer(ScanTimer, this, &ADoorRetrofitter::ScanAndConvert, 2.0f, true);
}

void ADoorRetrofitter::ScanAndConvert()
{
	UWorld* W = GetWorld();
	if (!W) { return; }

	int32 NewThisPass = 0;
	for (TActorIterator<AStaticMeshActor> It(W); It; ++It)
	{
		AStaticMeshActor* SMA = *It;
		if (!IsValid(SMA) || Converted.Contains(SMA)) { continue; }
		UStaticMeshComponent* Comp = SMA->GetStaticMeshComponent();
		const FLeafDef* Leaf = Comp ? MatchLeaf(Comp->GetStaticMesh()) : nullptr;
		if (!Leaf) { continue; }

		// Origineel blad verbergen + collision uit (verwijderen kan niet altijd in gestreamde levels).
		const FTransform LeafTM = Comp->GetComponentTransform();
		SMA->SetActorHiddenInGame(true);
		SMA->SetActorEnableCollision(false);
		Converted.Add(SMA);

		FActorSpawnParameters SP; SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		if (ACityDoor* Door = W->SpawnActor<ACityDoor>(ACityDoor::StaticClass(), LeafTM, SP))
		{
			Door->SetActorScale3D(LeafTM.GetScale3D());
			Door->SetupLeaf(Comp->GetStaticMesh(), Leaf->OpenDeg);
			SpawnedDoors.Add(Door);
			++NewThisPass;
			++TotalConverted;
		}
	}

	// Glas-pass: losse glas-meshes van de bladen aan de dichtstbijzijnde geconverteerde deur hangen,
	// zodat het raam met de deur mee draait i.p.v. in de lucht te blijven hangen.
	for (TActorIterator<AStaticMeshActor> It(W); It; ++It)
	{
		AStaticMeshActor* SMA = *It;
		if (!IsValid(SMA) || Converted.Contains(SMA)) { continue; }
		UStaticMeshComponent* Comp = SMA->GetStaticMeshComponent();
		if (!Comp || !IsLeafGlass(Comp->GetStaticMesh())) { continue; }

		const FVector GlassLoc = Comp->GetComponentLocation();
		ACityDoor* Best = nullptr; float BestD = 60.f;
		for (const TWeakObjectPtr<ACityDoor>& Dw : SpawnedDoors)
		{
			ACityDoor* D = Dw.Get();
			if (!D) { continue; }
			const float Dist = FVector::Dist(D->GetActorLocation(), GlassLoc);
			if (Dist < BestD) { BestD = Dist; Best = D; }
		}
		if (!Best) { continue; } // blad nog niet geconverteerd (gestreamd?) -> volgende pass opnieuw

		const FTransform GlassTM = Comp->GetComponentTransform();
		SMA->SetActorHiddenInGame(true);
		SMA->SetActorEnableCollision(false);
		Converted.Add(SMA);
		Best->AddLeafExtra(Comp->GetStaticMesh(), GlassTM);
	}

	if (NewThisPass > 0)
	{
		UE_LOG(LogWeedShop, Log, TEXT("DoorRetrofitter: %d nieuwe deuren werkend gemaakt (totaal %d)"), NewThisPass, TotalConverted);
	}
}
