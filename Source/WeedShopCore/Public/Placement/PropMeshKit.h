// PropMeshKit — kleine helper om in-world objecten op te bouwen uit meerdere basis-vormen (blok,
// cilinder) i.p.v. één saaie kubus. Bedoeld om vanuit een actor-CONSTRUCTOR te gebruiken
// (CreateDefaultSubobject). Maten in centimeters (volledige afmeting, niet half).

#pragma once

#include "CoreMinimal.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/EngineTypes.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/UObjectGlobals.h"

namespace PropKit
{
	// LET OP: deze helpers worden óók runtime aangeroepen (in SetupVisual), dus GEEN
	// ConstructorHelpers::FObjectFinder (die mag alleen in constructors). LoadObject werkt overal.
	inline UStaticMesh* Cube()
	{
		return LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	}
	inline UStaticMesh* Cylinder()
	{
		return LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	}
	inline UStaticMesh* Sphere()
	{
		return LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	}
	inline UStaticMesh* Cone()
	{
		return LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cone.Cone"));
	}
	inline UMaterialInterface* BaseMat()
	{
		return LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	}

	// Maak een "deco-wortel": hangt aan de (vaak niet-uniform geschaalde) root maar negeert die schaal,
	// zodat onderdelen eronder in échte centimeters te plaatsen zijn. In de CONSTRUCTOR aanroepen.
	inline USceneComponent* MakeDeco(AActor* Owner, USceneComponent* Root, const TCHAR* Name)
	{
		USceneComponent* D = Owner->CreateDefaultSubobject<USceneComponent>(Name);
		D->SetupAttachment(Root);
		D->SetUsingAbsoluteScale(true); // negeer root-schaal -> kinderen in echte cm
		return D;
	}

	// Maak een (eerst verborgen) onderdeel-mesh onder de deco-wortel. In de CONSTRUCTOR aanroepen.
	inline UStaticMeshComponent* MakePart(AActor* Owner, USceneComponent* Deco, const TCHAR* Name)
	{
		UStaticMeshComponent* C = Owner->CreateDefaultSubobject<UStaticMeshComponent>(Name);
		C->SetupAttachment(Deco);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetVisibility(false);
		if (UStaticMesh* Cu = Cube()) { C->SetStaticMesh(Cu); }
		return C;
	}

	// Configureer een bestaand onderdeel (runtime, bv. in SetupVisual). SizeCm = volledige afmeting.
	inline void SetPart(UStaticMeshComponent* C, UStaticMesh* MeshAsset, const FVector& SizeCm,
		const FVector& LocCm, const FLinearColor& Color, const FRotator& Rot = FRotator::ZeroRotator)
	{
		if (!C) { return; }
		C->SetVisibility(true);
		if (MeshAsset) { C->SetStaticMesh(MeshAsset); }
		C->SetRelativeScale3D(SizeCm / 100.f);
		C->SetRelativeLocation(LocCm);
		C->SetRelativeRotation(Rot);
		UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(C->GetMaterial(0));
		if (!MID) { if (UMaterialInterface* M = BaseMat()) { MID = C->CreateDynamicMaterialInstance(0, M); } }
		if (MID) { MID->SetVectorParameterValue(TEXT("Color"), Color); }
	}

	// Representatief in-hand/in-world 3D-model voor een item: zet mesh + schaal + kleur op één component.
	// Eén vorm per categorie (cm). Gebruikt voor het hand-model EN het gedropte wereld-item (zelfde look).
	inline void ApplyItemModel(UStaticMeshComponent* C, FName ItemId, float ScaleMul = 1.f)
	{
		if (!C) { return; }
		const FString S = ItemId.ToString();
		UStaticMesh* M = Cube();
		FVector Size(8.f, 8.f, 8.f);
		FLinearColor Col(0.55f, 0.57f, 0.62f);
		FRotator Rot = FRotator::ZeroRotator;
		if      (S.StartsWith(TEXT("WetBud_")))     { Size = FVector(7.f, 7.f, 7.f);  Col = FLinearColor(0.20f, 0.42f, 0.18f); }
		else if (S.StartsWith(TEXT("Bud_")))        { Size = FVector(7.f, 7.f, 7.f);  Col = FLinearColor(0.26f, 0.55f, 0.22f); }
		else if (S.StartsWith(TEXT("Bag_")))        { Size = FVector(8.f, 5.f, 10.f); Col = FLinearColor(0.70f, 0.62f, 0.42f); }
		else if (S.StartsWith(TEXT("Joint_")))      { M = Cylinder(); Size = FVector(1.8f, 1.8f, 9.f); Col = FLinearColor(0.93f, 0.92f, 0.85f); Rot = FRotator(0.f, 0.f, 90.f); }
		else if (S.StartsWith(TEXT("Crystal_")))    { Size = FVector(6.f, 6.f, 6.f);  Col = FLinearColor(0.80f, 0.85f, 0.95f); }
		else if (S.StartsWith(TEXT("Hash_")))       { Size = FVector(6.f, 6.f, 4.f);  Col = FLinearColor(0.35f, 0.22f, 0.12f); }
		else if (S.StartsWith(TEXT("Seed_")))       { Size = FVector(2.5f, 2.5f, 2.5f); Col = FLinearColor(0.45f, 0.36f, 0.22f); }
		else if (S.StartsWith(TEXT("WaterBottle"))) { M = Cylinder(); Size = FVector(6.f, 6.f, 16.f); Col = FLinearColor(0.30f, 0.45f, 0.65f); }
		else if (S.StartsWith(TEXT("Papers_")))     { Size = FVector(6.f, 1.5f, 9.f); Col = FLinearColor(0.90f, 0.88f, 0.80f); }
		else if (S.StartsWith(TEXT("Soil_")))       { Size = FVector(9.f, 6.f, 11.f); Col = FLinearColor(0.30f, 0.22f, 0.16f); }
		else if (S.StartsWith(TEXT("Pot_")))        { M = Cylinder(); Size = FVector(12.f, 12.f, 11.f); Col = FLinearColor(0.60f, 0.35f, 0.22f); }
		else if (S == TEXT("Cash"))                 { Size = FVector(8.f, 5.f, 3.f);  Col = FLinearColor(0.20f, 0.60f, 0.30f); }
		SetPart(C, M, Size * ScaleMul, FVector::ZeroVector, Col, Rot);
	}

	// === Echt herkenbaar item-model (meerdere onderdelen), runtime opgebouwd onder een parent-component. ===
	// Verwijder alle bestaande onderdeel-meshes onder de parent (om opnieuw te bouwen).
	inline void ClearItemModel(USceneComponent* Parent)
	{
		if (!Parent) { return; }
		TArray<USceneComponent*> Kids;
		Parent->GetChildrenComponents(false, Kids);
		for (USceneComponent* K : Kids) { if (K) { K->DestroyComponent(); } }
	}

	// Maak één onderdeel-mesh aan (runtime). bFirstPerson = alleen-eigenaar + FP-render (voor het hand-model).
	// bCollision = blokkeer traces (voor het wereld-item zodat je 't kunt aankijken/oppakken).
	inline UStaticMeshComponent* SpawnItemPart(AActor* Owner, USceneComponent* Parent, UStaticMesh* M,
		const FVector& SizeCm, const FVector& LocCm, const FLinearColor& Col, const FRotator& Rot,
		bool bFirstPerson, bool bCollision)
	{
		if (!Owner || !Parent || !M) { return nullptr; }
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(Owner);
		if (!C) { return nullptr; }
		C->SetupAttachment(Parent);
		C->SetCastShadow(false);
		if (bCollision) { C->SetCollisionEnabled(ECollisionEnabled::QueryOnly); C->SetCollisionResponseToAllChannels(ECR_Block); }
		else { C->SetCollisionEnabled(ECollisionEnabled::NoCollision); }
		if (bFirstPerson) { C->SetOnlyOwnerSee(true); C->FirstPersonPrimitiveType = EFirstPersonPrimitiveType::FirstPerson; }
		C->RegisterComponent();
		C->SetStaticMesh(M);
		C->SetRelativeLocationAndRotation(LocCm, Rot);
		C->SetRelativeScale3D(SizeCm / 100.f);
		if (UMaterialInterface* BM = BaseMat())
		{
			if (UMaterialInstanceDynamic* MID = C->CreateDynamicMaterialInstance(0, BM)) { MID->SetVectorParameterValue(TEXT("Color"), Col); }
		}
		return C;
	}

	// Bouw een herkenbaar model per item (uit basis-vormen) onder de parent. Gebruikt voor hand + wereld-item.
	inline void BuildItemModel(AActor* Owner, USceneComponent* Parent, FName ItemId, float ScaleMul = 1.f,
		bool bFirstPerson = false, bool bCollision = false)
	{
		if (!Owner || !Parent) { return; }
		ClearItemModel(Parent);
		const FString S = ItemId.ToString();
		UStaticMesh* Cu = Cube(); UStaticMesh* Cy = Cylinder(); UStaticMesh* Sp = Sphere(); UStaticMesh* Co = Cone();
		auto P = [&](UStaticMesh* M, const FVector& Size, const FVector& Loc, const FLinearColor& Col, const FRotator& Rot = FRotator::ZeroRotator)
		{ SpawnItemPart(Owner, Parent, M, Size * ScaleMul, Loc * ScaleMul, Col, Rot, bFirstPerson, bCollision); };

		if (S.StartsWith(TEXT("WetBud_")) || S.StartsWith(TEXT("Bud_")))
		{
			const FLinearColor G = S.StartsWith(TEXT("WetBud_")) ? FLinearColor(0.17f, 0.34f, 0.15f) : FLinearColor(0.26f, 0.52f, 0.22f);
			P(Sp, FVector(6, 6, 6), FVector(0, 0, 0), G);
			P(Sp, FVector(4.2f, 4.2f, 4.2f), FVector(2.6f, 1.0f, 1.8f), G * 1.12f);
			P(Sp, FVector(4.0f, 4.0f, 4.0f), FVector(-2.2f, 1.8f, 1.4f), G * 0.9f);
			P(Sp, FVector(3.6f, 3.6f, 3.6f), FVector(0.4f, -2.4f, 2.4f), G);
			P(Co, FVector(1.0f, 1.0f, 3.0f), FVector(0.5f, 0, 4.2f), FLinearColor(0.72f, 0.4f, 0.15f)); // oranje haartje
		}
		else if (S.StartsWith(TEXT("Bag_")))
		{
			P(Cu, FVector(7, 2.6f, 8), FVector(0, 0, 0), FLinearColor(0.74f, 0.71f, 0.55f)); // zakje (plat)
			P(Sp, FVector(4, 2.0f, 4), FVector(0, 0, -1.0f), FLinearColor(0.25f, 0.5f, 0.22f)); // groene inhoud
			P(Cu, FVector(7.6f, 3.0f, 1.6f), FVector(0, 0, 4.4f), FLinearColor(0.5f, 0.5f, 0.46f)); // zip-sluiting
		}
		else if (S.StartsWith(TEXT("Joint_")))
		{
			P(Cy, FVector(1.7f, 1.7f, 11), FVector(0, 0, 0), FLinearColor(0.95f, 0.94f, 0.88f)); // vloei
			P(Cy, FVector(1.8f, 1.8f, 2.4f), FVector(0, 0, -5.3f), FLinearColor(0.78f, 0.6f, 0.35f)); // filter
			P(Sp, FVector(1.5f, 1.5f, 1.5f), FVector(0, 0, 5.8f), FLinearColor(1.0f, 0.45f, 0.12f)); // gloed
		}
		else if (S.StartsWith(TEXT("WaterBottle")))
		{
			P(Cy, FVector(6, 6, 13), FVector(0, 0, 0), FLinearColor(0.30f, 0.5f, 0.7f)); // fles
			P(Cy, FVector(3, 3, 3), FVector(0, 0, 7.6f), FLinearColor(0.30f, 0.5f, 0.7f)); // hals
			P(Cy, FVector(3.4f, 3.4f, 2), FVector(0, 0, 9.4f), FLinearColor(0.15f, 0.4f, 0.2f)); // dop
		}
		else if (S.StartsWith(TEXT("Seed_")))
		{
			P(Sp, FVector(2.6f, 2.6f, 3.4f), FVector(0, 0, 0), FLinearColor(0.5f, 0.4f, 0.25f)); // zaadje
			P(Cu, FVector(0.5f, 2.7f, 3.4f), FVector(0, 0, 0), FLinearColor(0.3f, 0.24f, 0.14f)); // streep
		}
		else if (S.StartsWith(TEXT("Crystal_")))
		{
			const FLinearColor K(0.82f, 0.86f, 0.95f);
			P(Cu, FVector(3, 3, 3), FVector(0, 0, 0), K, FRotator(20, 30, 15));
			P(Cu, FVector(2.4f, 2.4f, 2.4f), FVector(2.6f, 1, 1), K, FRotator(40, 10, 25));
			P(Cu, FVector(2.0f, 2.0f, 2.0f), FVector(-2.1f, 1.4f, 1.4f), K, FRotator(10, 50, 5));
		}
		else if (S.StartsWith(TEXT("Hash_")))
		{
			P(Cu, FVector(6, 6, 3.6f), FVector(0, 0, 0), FLinearColor(0.32f, 0.2f, 0.11f)); // blok
			P(Cu, FVector(6.2f, 2, 3.8f), FVector(0, 0, 0), FLinearColor(0.42f, 0.28f, 0.16f)); // lichtere streep
		}
		else if (S.StartsWith(TEXT("Papers_")))
		{
			P(Cu, FVector(5, 1.3f, 8), FVector(0, 0, 0), FLinearColor(0.9f, 0.88f, 0.8f)); // boekje
			P(Cu, FVector(5.2f, 1.5f, 2.6f), FVector(0, 0.2f, 5.2f), FLinearColor(0.75f, 0.6f, 0.35f)); // kaft-flap
		}
		else if (S.StartsWith(TEXT("Soil_")))
		{
			P(Cu, FVector(8, 6, 10), FVector(0, 0, 0), FLinearColor(0.32f, 0.23f, 0.16f)); // zak
			P(Cu, FVector(8.4f, 6.4f, 2.6f), FVector(0, 0, 5.5f), FLinearColor(0.26f, 0.18f, 0.12f)); // gevouwen top
			P(Cu, FVector(5, 0.3f, 4), FVector(0, 3.1f, 0), FLinearColor(0.55f, 0.45f, 0.3f)); // label
		}
		else if (S.StartsWith(TEXT("Pot_")))
		{
			P(Co, FVector(11, 11, 11), FVector(0, 0, 0), FLinearColor(0.62f, 0.34f, 0.2f), FRotator(180, 0, 0)); // pot (omgekeerde kegel)
			P(Cy, FVector(12, 12, 2), FVector(0, 0, 5.2f), FLinearColor(0.55f, 0.3f, 0.18f)); // rand
			P(Cy, FVector(10, 10, 1.5f), FVector(0, 0, 4.3f), FLinearColor(0.2f, 0.15f, 0.1f)); // aarde
		}
		else if (S == TEXT("Cash"))
		{
			const FLinearColor Money(0.25f, 0.55f, 0.32f);
			P(Cu, FVector(8, 5, 1.2f), FVector(0, 0, 0), Money);
			P(Cu, FVector(8, 5, 1.2f), FVector(0.4f, 0.3f, 1.3f), Money * 1.12f);
			P(Cu, FVector(8, 5, 1.2f), FVector(-0.3f, -0.2f, 2.6f), Money * 0.92f);
			P(Cu, FVector(2, 5.4f, 4), FVector(0, 0, 1.3f), FLinearColor(0.85f, 0.3f, 0.3f)); // bandje
		}
		else
		{
			P(Cu, FVector(7, 7, 7), FVector(0, 0, 0), FLinearColor(0.6f, 0.6f, 0.65f));
		}
	}

	// Voeg een onderdeel toe. SizeCm = volledige afmeting (de basis-kubus is 100cm, dus schaal = Size/100).
	// LocCm = midden t.o.v. de parent-origin. Geen collision (alleen de root draagt collision).
	inline UStaticMeshComponent* AddPart(AActor* Owner, USceneComponent* Parent, const TCHAR* Name,
		UStaticMesh* MeshAsset, const FVector& SizeCm, const FVector& LocCm, const FLinearColor& Color,
		const FRotator& Rot = FRotator::ZeroRotator)
	{
		if (!Owner || !MeshAsset) { return nullptr; }
		UStaticMeshComponent* C = Owner->CreateDefaultSubobject<UStaticMeshComponent>(Name);
		C->SetupAttachment(Parent);
		C->SetStaticMesh(MeshAsset);
		C->SetRelativeScale3D(SizeCm / 100.f);
		C->SetRelativeLocation(LocCm);
		C->SetRelativeRotation(Rot);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetCastShadow(true);
		if (UMaterialInterface* M = BaseMat())
		{
			if (UMaterialInstanceDynamic* MID = C->CreateDynamicMaterialInstance(0, M))
			{
				MID->SetVectorParameterValue(TEXT("Color"), Color);
			}
		}
		return C;
	}
}
