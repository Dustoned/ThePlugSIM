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
