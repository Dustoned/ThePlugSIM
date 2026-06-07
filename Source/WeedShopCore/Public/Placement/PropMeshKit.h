// PropMeshKit — kleine helper om in-world objecten op te bouwen uit meerdere basis-vormen (blok,
// cilinder) i.p.v. één saaie kubus. Bedoeld om vanuit een actor-CONSTRUCTOR te gebruiken
// (CreateDefaultSubobject). Maten in centimeters (volledige afmeting, niet half).

#pragma once

#include "CoreMinimal.h"
#include "Components/StaticMeshComponent.h"
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
