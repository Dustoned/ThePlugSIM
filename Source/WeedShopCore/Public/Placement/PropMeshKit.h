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

	// Deterministische hash van een naam -> stabiele index/kleur (zelfde strain = altijd dezelfde tint).
	inline int32 NameHash(const FString& Key)
	{
		uint32 h = 2166136261u;
		for (int32 i = 0; i < Key.Len(); ++i) { h = (h ^ (uint32)Key[i]) * 16777619u; }
		return (int32)(h & 0x7fffffff);
	}
	// Onderscheidende accent-kleur per strain/variant (zodat verschillende strains duidelijk verschillen).
	inline FLinearColor StrainAccent(const FString& Key)
	{
		static const FLinearColor Acc[] = {
			FLinearColor(0.50f, 0.60f, 0.18f), // lime
			FLinearColor(0.42f, 0.24f, 0.55f), // paars
			FLinearColor(0.64f, 0.42f, 0.15f), // amber
			FLinearColor(0.20f, 0.58f, 0.42f), // teal
			FLinearColor(0.62f, 0.28f, 0.30f), // rood
			FLinearColor(0.26f, 0.46f, 0.64f), // blauw
			FLinearColor(0.58f, 0.56f, 0.22f), // geel-groen
			FLinearColor(0.52f, 0.34f, 0.62f), // violet
			FLinearColor(0.30f, 0.60f, 0.25f), // fris groen
			FLinearColor(0.66f, 0.36f, 0.20f), // oranje-bruin
		};
		return Acc[NameHash(Key) % (int32)(sizeof(Acc) / sizeof(Acc[0]))];
	}

	// Bouw een herkenbaar model per item (uit basis-vormen) onder de parent. Gebruikt voor hand + wereld-item.
	inline void BuildItemModel(AActor* Owner, USceneComponent* Parent, FName ItemId, float ScaleMul = 1.f,
		bool bFirstPerson = false, bool bCollision = false)
	{
		if (!Owner || !Parent) { return; }
		ClearItemModel(Parent);
		const FString S = ItemId.ToString();
		// Strain/variant uit de naam (bv. Bud_SilverHaze -> "SilverHaze") -> onderscheidende accent-kleur.
		TArray<FString> Toks; S.ParseIntoArray(Toks, TEXT("_"));
		const FString StrainKey = Toks.Num() >= 2 ? Toks[1] : S;
		const FLinearColor Accent = StrainAccent(StrainKey);
		UStaticMesh* Cu = Cube(); UStaticMesh* Cy = Cylinder(); UStaticMesh* Sp = Sphere(); UStaticMesh* Co = Cone();
		auto P = [&](UStaticMesh* M, const FVector& Size, const FVector& Loc, const FLinearColor& Col, const FRotator& Rot = FRotator::ZeroRotator)
		{ SpawnItemPart(Owner, Parent, M, Size * ScaleMul, Loc * ScaleMul, Col, Rot, bFirstPerson, bCollision); };

		if (S.StartsWith(TEXT("WetBud_")) || S.StartsWith(TEXT("Bud_")))
		{
			const bool bWet = S.StartsWith(TEXT("WetBud_"));
			const FLinearColor baseG = bWet ? FLinearColor(0.16f, 0.33f, 0.14f) : FLinearColor(0.27f, 0.54f, 0.23f);
			const FLinearColor G = FMath::Lerp(baseG, Accent, 0.42f);  // strain-tint -> elk soort eigen kleur
			const FLinearColor G2 = FMath::Lerp(baseG, Accent, 0.62f); // diepere accent-plekken
			const FLinearColor Gl = G * 1.28f;                          // frosty topjes
			P(Sp, FVector(6.4f, 6.4f, 7.4f), FVector(0, 0, 0), G);                       // kern
			P(Sp, FVector(4.6f, 4.6f, 4.6f), FVector(2.7f, 1.0f, 2.0f), Gl);
			P(Sp, FVector(4.4f, 4.4f, 4.4f), FVector(-2.5f, 1.6f, 1.5f), G2);
			P(Sp, FVector(4.0f, 4.0f, 4.0f), FVector(0.6f, -2.6f, 2.7f), Gl);
			P(Sp, FVector(3.8f, 3.8f, 3.8f), FVector(1.6f, 2.2f, -1.8f), G2);
			P(Sp, FVector(3.4f, 3.4f, 3.4f), FVector(-1.9f, -1.6f, -1.9f), Gl);
			P(Sp, FVector(3.2f, 3.2f, 3.2f), FVector(2.0f, -1.4f, -2.2f), G);
			P(Sp, FVector(2.8f, 2.8f, 2.8f), FVector(-1.0f, 2.4f, 3.4f), Gl); // frosty kroon
			P(Co, FVector(0.9f, 0.9f, 3.4f), FVector(0.8f, 0.2f, 4.9f), FLinearColor(0.85f, 0.45f, 0.14f));            // oranje haartje
			P(Co, FVector(0.8f, 0.8f, 2.8f), FVector(-1.6f, 0.7f, 4.1f), FLinearColor(0.78f, 0.38f, 0.12f), FRotator(20, 0, 30));
			P(Co, FVector(0.7f, 0.7f, 2.4f), FVector(1.8f, 1.2f, 3.6f), FLinearColor(0.80f, 0.40f, 0.13f), FRotator(15, 0, -25));
			P(Cy, FVector(0.9f, 0.9f, 2.6f), FVector(0, 0, -4.4f), FLinearColor(0.30f, 0.40f, 0.20f));                 // stengeltje
		}
		else if (S.StartsWith(TEXT("Bag_")))
		{
			const FLinearColor Buds = FMath::Lerp(FLinearColor(0.24f, 0.48f, 0.21f), Accent, 0.5f); // strain-kleur in 't zakje
			P(Cu, FVector(7.0f, 2.6f, 7.4f), FVector(0, 0, -0.3f), FLinearColor(0.82f, 0.80f, 0.66f)); // zakje (lichte folie)
			P(Sp, FVector(6.8f, 2.6f, 3.4f), FVector(0, 0, -4.0f), FLinearColor(0.82f, 0.80f, 0.66f)); // ronde bodem
			P(Sp, FVector(3.2f, 1.9f, 3.2f), FVector(1.1f, 0, -1.4f), Buds);          // bud inhoud
			P(Sp, FVector(2.8f, 1.8f, 2.8f), FVector(-1.5f, 0, -3.0f), Buds * 0.85f);
			P(Sp, FVector(2.4f, 1.6f, 2.4f), FVector(0.2f, 0, 0.6f), Buds * 1.12f);
			P(Cu, FVector(7.6f, 3.0f, 1.2f), FVector(0, 0, 3.9f), FLinearColor(0.55f, 0.55f, 0.50f)); // zip-strook
			P(Cu, FVector(7.7f, 3.2f, 0.4f), FVector(0, 0, 3.2f), FLinearColor(0.42f, 0.42f, 0.40f)); // zip-lijn
			P(Cu, FVector(2.8f, 0.3f, 1.6f), FVector(-1.6f, 1.35f, 1.5f), Accent); // gekleurd merk-stickertje
		}
		else if (S.StartsWith(TEXT("Joint_")))
		{
			P(Cy, FVector(1.8f, 1.8f, 10.2f), FVector(0, 0, 0), FLinearColor(0.96f, 0.95f, 0.90f));  // vloei
			P(Co, FVector(1.8f, 1.8f, 2.6f), FVector(0, 0, 6.2f), FLinearColor(0.90f, 0.88f, 0.80f)); // gedraaide top
			P(Cy, FVector(1.9f, 1.9f, 2.6f), FVector(0, 0, -5.0f), FLinearColor(0.76f, 0.58f, 0.34f)); // filter
			P(Cy, FVector(1.95f, 1.95f, 0.6f), FVector(0, 0, -3.6f), Accent);                          // strain-bandje
			P(Sp, FVector(1.7f, 1.7f, 1.7f), FVector(0, 0, 7.4f), FLinearColor(1.0f, 0.5f, 0.12f));    // gloed
			P(Sp, FVector(0.9f, 0.9f, 0.9f), FVector(0, 0, 7.9f), FLinearColor(1.0f, 0.85f, 0.30f));   // hete kern
		}
		else if (S.StartsWith(TEXT("WaterBottle")))
		{
			P(Cy, FVector(6.2f, 6.2f, 11.0f), FVector(0, 0, -0.5f), FLinearColor(0.32f, 0.52f, 0.72f)); // fles
			P(Cy, FVector(6.5f, 6.5f, 0.9f), FVector(0, 0, 2.6f), FLinearColor(0.28f, 0.46f, 0.66f));   // ribbel
			P(Cy, FVector(6.5f, 6.5f, 0.9f), FVector(0, 0, -1.6f), FLinearColor(0.28f, 0.46f, 0.66f));  // ribbel
			P(Cy, FVector(6.45f, 6.45f, 3.4f), FVector(0, 0, -1.5f), FLinearColor(0.95f, 0.95f, 0.97f)); // label
			P(Cy, FVector(3.2f, 3.2f, 3.0f), FVector(0, 0, 6.0f), FLinearColor(0.32f, 0.52f, 0.72f));   // hals
			P(Cy, FVector(3.7f, 3.7f, 2.2f), FVector(0, 0, 7.8f), FLinearColor(0.15f, 0.40f, 0.20f));   // dop
		}
		else if (S.StartsWith(TEXT("Seed_")))
		{
			const FLinearColor SeedC = FMath::Lerp(FLinearColor(0.52f, 0.42f, 0.27f), Accent, 0.3f); // lichte strain-tint
			P(Sp, FVector(2.7f, 2.9f, 3.6f), FVector(0, 0, 0), SeedC);                               // ovaal zaadje
			P(Co, FVector(1.7f, 1.8f, 1.9f), FVector(0, 0, 2.1f), SeedC * 0.95f);                    // puntje
			P(Cu, FVector(0.4f, 3.0f, 3.6f), FVector(0, 0, 0), FLinearColor(0.28f, 0.22f, 0.13f));    // naad
			P(Cu, FVector(3.0f, 0.4f, 1.8f), FVector(0, 0, 0.5f), FLinearColor(0.62f, 0.52f, 0.34f), FRotator(0, 0, 30)); // tijger-streep
			P(Cu, FVector(3.0f, 0.4f, 1.2f), FVector(0, 0, -0.8f), FLinearColor(0.30f, 0.24f, 0.15f), FRotator(0, 0, -20)); // 2e streep
		}
		else if (S.StartsWith(TEXT("Crystal_")))
		{
			const FLinearColor K = FMath::Lerp(FLinearColor(0.84f, 0.88f, 0.97f), Accent, 0.22f);
			const FLinearColor K2 = FMath::Lerp(FLinearColor(0.70f, 0.78f, 0.92f), Accent, 0.30f);
			P(Cu, FVector(3.2f, 3.2f, 3.6f), FVector(0, 0, 0), K, FRotator(20, 30, 15));
			P(Cu, FVector(2.6f, 2.6f, 3.0f), FVector(2.6f, 1.0f, 0.5f), K2, FRotator(40, 10, 25));
			P(Cu, FVector(2.2f, 2.2f, 2.6f), FVector(-2.2f, 1.4f, 1.0f), K, FRotator(10, 50, 5));
			P(Cu, FVector(1.8f, 1.8f, 2.2f), FVector(0.5f, -2.2f, 1.2f), K2, FRotator(35, 20, 40));
			P(Sp, FVector(0.8f, 0.8f, 0.8f), FVector(1.2f, 0.5f, 2.6f), FLinearColor(1, 1, 1)); // glinster
		}
		else if (S.StartsWith(TEXT("Hash_")))
		{
			const FLinearColor HashC = FMath::Lerp(FLinearColor(0.30f, 0.19f, 0.10f), Accent, 0.18f);
			P(Cu, FVector(5.6f, 5.6f, 3.6f), FVector(0, 0, 0), HashC);                                 // blok
			P(Sp, FVector(3.0f, 3.0f, 2.6f), FVector(1.5f, 1.5f, 1.6f), HashC * 1.2f);                 // ronde brok
			P(Cu, FVector(5.8f, 1.6f, 3.8f), FVector(0, -0.5f, 0), HashC * 1.35f, FRotator(0, 0, 8)); // swirl
			P(Cu, FVector(2.4f, 2.4f, 2.4f), FVector(-2.5f, -2.0f, 1.7f), FLinearColor(0.24f, 0.15f, 0.08f), FRotator(15, 20, 10)); // afgebroken hoek
		}
		else if (S.StartsWith(TEXT("Papers_")))
		{
			P(Cu, FVector(5.0f, 1.5f, 8.0f), FVector(0, 0, 0), FLinearColor(0.90f, 0.88f, 0.82f));    // boekje
			P(Cu, FVector(5.2f, 0.5f, 8.2f), FVector(0, 0.55f, 0), FLinearColor(0.85f, 0.45f, 0.25f)); // gekleurde kaft
			P(Cu, FVector(4.4f, 0.4f, 2.2f), FVector(0, -0.2f, 5.0f), FLinearColor(0.97f, 0.96f, 0.92f), FRotator(12, 0, 0)); // velletje eruit
			P(Cu, FVector(2.6f, 0.6f, 1.2f), FVector(0, 0.62f, 0), FLinearColor(0.20f, 0.20f, 0.20f)); // merk-label
		}
		else if (S.StartsWith(TEXT("Soil_")))
		{
			P(Cu, FVector(7.5f, 5.8f, 9.0f), FVector(0, 0, -0.5f), FLinearColor(0.33f, 0.24f, 0.17f)); // zak
			P(Cu, FVector(7.9f, 6.2f, 2.4f), FVector(0, 0, 5.0f), FLinearColor(0.26f, 0.18f, 0.12f));  // gevouwen top
			P(Cu, FVector(2.0f, 6.3f, 2.0f), FVector(0, 0, 5.8f), FLinearColor(0.24f, 0.16f, 0.10f), FRotator(0, 0, 90)); // vouw
			P(Cu, FVector(5.0f, 0.3f, 4.5f), FVector(0, 2.95f, -0.5f), FLinearColor(0.60f, 0.85f, 0.45f)); // groen label
			P(Sp, FVector(1.4f, 1.4f, 1.0f), FVector(1.5f, 0, 5.8f), FLinearColor(0.20f, 0.14f, 0.08f)); // gemorste aarde
		}
		else if (S.StartsWith(TEXT("Pot_")))
		{
			P(Co, FVector(10.5f, 10.5f, 10.0f), FVector(0, 0, -0.5f), FLinearColor(0.64f, 0.36f, 0.22f), FRotator(180, 0, 0)); // taps pot
			P(Cy, FVector(11.5f, 11.5f, 2.2f), FVector(0, 0, 4.8f), FLinearColor(0.58f, 0.32f, 0.19f)); // rand
			P(Cy, FVector(9.6f, 9.6f, 1.6f), FVector(0, 0, 4.1f), FLinearColor(0.18f, 0.13f, 0.09f));   // aarde
			P(Cy, FVector(6.6f, 6.6f, 1.2f), FVector(0, 0, -5.0f), FLinearColor(0.50f, 0.28f, 0.16f));  // voet/schotel
		}
		else if (S == TEXT("Cash"))
		{
			const FLinearColor Money(0.26f, 0.56f, 0.33f);
			P(Cu, FVector(8, 5, 1.0f), FVector(0, 0, 0), Money);
			P(Cu, FVector(8, 5, 1.0f), FVector(0.3f, 0.2f, 1.0f), Money * 1.08f);
			P(Cu, FVector(8, 5, 1.0f), FVector(-0.2f, 0.4f, 2.0f), Money * 0.95f);
			P(Cu, FVector(8, 5, 1.0f), FVector(0.4f, -0.2f, 3.0f), Money * 1.05f);
			P(Cu, FVector(2.2f, 5.4f, 4.4f), FVector(0, 0, 1.5f), FLinearColor(0.88f, 0.32f, 0.32f)); // bandje
			P(Cu, FVector(1.8f, 1.8f, 0.3f), FVector(0, 0, 3.6f), FLinearColor(0.90f, 0.85f, 0.40f)); // stempel
		}
		else
		{
			P(Cu, FVector(6.5f, 6.5f, 6.5f), FVector(0, 0, 0), FLinearColor(0.60f, 0.60f, 0.65f));
			P(Cu, FVector(7.0f, 1.0f, 7.0f), FVector(0, 0, 0), FLinearColor(0.50f, 0.50f, 0.55f), FRotator(0, 0, 45)); // "?"-markering
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
