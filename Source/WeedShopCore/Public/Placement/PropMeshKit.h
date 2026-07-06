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
#include "Placement/PlaceableTypes.h"
#include "Inventory/InventoryComponent.h"

// Forward-decl (geen zware UI-header hier): de per-strain kleur uit WeedUiStyle, zodat de FYSIEKE
// gedropte bag/seed dezelfde strain-kleur krijgt als het UI-icoon -> je ziet aan de kleur wat er ligt.
namespace WeedUI { WEEDSHOPCORE_API FLinearColor TagColorForItem(FName ItemId, float Value, float Sat); }

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

	// === Echte joint-meshes (gescande CC-BY-modellen, attributie in Docs/CREDITS.md) =================
	// Doellengtes in de wereld (cm), speler-eis "human size": klein jointje ~13 cm, dikke ~17 cm.
	// De uniform-schaal wordt uit de ÉCHTE mesh-bounds berekend (geen magic numbers), zodat een
	// re-import met andere maat vanzelf goed blijft.
	constexpr float JointSmallTargetLenCm = 13.f;
	constexpr float JointFatTargetLenCm   = 17.f;

	// Laad + cache een joint-mesh (zelfde idioom als WeedUI::LoadByStem: één keer laden, AddToRoot
	// zodat de GC 'm niet opruimt; ook een gemiste load wordt gecached zodat we niet blijven proberen).
	inline UStaticMesh* LoadJointMesh(const TCHAR* Path)
	{
		static TMap<FString, UStaticMesh*> Cache;
		if (UStaticMesh** Found = Cache.Find(Path)) { return *Found; }
		UStaticMesh* M = LoadObject<UStaticMesh>(nullptr, Path);
		if (M) { M->AddToRoot(); }
		Cache.Add(Path, M);
		return M;
	}

	// Kies het echte joint-model + transform voor een joint-item. Gram-tier bepaalt het model:
	// 2g/5g -> SM_JointSmall (gescand, STAAND langs Z gemodelleerd), 7g/10g -> SM_JointFat (ligt al
	// langs X). Alles genormaliseerd: het model komt HORIZONTAAL langs X te liggen (physics-doos
	// wordt dan lang-en-plat -> settelt altijd op de zij), gecentreerd op de parent-origin.
	// OutTipLocCm = de punt aan de +X-kant (niet-filter-kant) voor de gloed-bolletjes.
	// False als het asset mist -> caller valt terug op het oude procedurele model.
	inline bool GetJointMeshModel(FName ItemId, UStaticMesh*& OutMesh, float& OutUniformScale,
		FRotator& OutRot, FVector& OutCenterOffsetCm, FVector& OutTipLocCm)
	{
		const int32 Grams = UInventoryComponent::JointGrams(ItemId);
		const bool bFat = Grams >= 7; // gram-tiers: 2/5 = klein, 7/10 = dik
		OutMesh = LoadJointMesh(bFat
			? TEXT("/Game/_Project/Models/Joints/SM_JointFat.SM_JointFat")
			: TEXT("/Game/_Project/Models/Joints/SM_JointSmall.SM_JointSmall"));
		if (!OutMesh) { return false; }
		const FBoxSphereBounds B = OutMesh->GetBounds();
		const float HalfLen = B.BoxExtent.GetMax(); // lange as = grootste bounds-extent
		if (HalfLen < KINDA_SMALL_NUMBER) { return false; }
		const float TargetLen = bFat ? JointFatTargetLenCm : JointSmallTargetLenCm;
		OutUniformScale = TargetLen / (HalfLen * 2.f);
		// Staand (Z-lang) gescand model plat leggen: Pitch -90 mapt +Z (gedraaide top) -> +X.
		// Een X-lang model ligt al goed.
		const bool bLongAxisZ = (B.BoxExtent.Z >= B.BoxExtent.X && B.BoxExtent.Z >= B.BoxExtent.Y);
		OutRot = bLongAxisZ ? FRotator(-90.f, 0.f, 0.f) : FRotator::ZeroRotator;
		// Centreer het visuele midden op de parent-origin (bounds-origin ligt niet per se op de pivot).
		OutCenterOffsetCm = -(OutRot.RotateVector(B.Origin) * OutUniformScale);
		OutTipLocCm = FVector(TargetLen * 0.5f, 0.f, 0.f);
		return true;
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
		else if (S.StartsWith(TEXT("Bag_")))        { Size = FVector(8.f, 5.f, 10.f); Col = WeedUI::TagColorForItem(ItemId, 0.55f, 0.7f); } // strain-kleur (matcht het UI-icoon)
		else if (S.StartsWith(TEXT("Joint_")))
		{
			// Echt gescand joint-model (gram-tier kiest klein/dik), liggend langs X, met z'n eigen
			// scan-materiaal (dus geen kleur-MID eroverheen).
			UStaticMesh* JM = nullptr; float JScale = 1.f; FRotator JRot = FRotator::ZeroRotator; FVector JOff = FVector::ZeroVector, JTip = FVector::ZeroVector;
			if (GetJointMeshModel(ItemId, JM, JScale, JRot, JOff, JTip))
			{
				C->SetVisibility(true);
				C->SetStaticMesh(JM);
				C->SetRelativeScale3D(FVector(JScale * ScaleMul));
				C->SetRelativeLocation(JOff * ScaleMul);
				C->SetRelativeRotation(JRot);
				C->EmptyOverrideMaterials(); // evt. oude kleur-MID van een vorig item weg -> scan-materiaal zichtbaar
				return;
			}
			// Fallback (asset mist): het oude procedurele cilinder-model, nooit een kale kubus.
			M = Cylinder(); Size = FVector(1.3f, 1.3f, 6.5f); Col = FLinearColor(0.93f, 0.92f, 0.85f); Rot = FRotator(0.f, 0.f, 90.f);
		}
		else if (S.StartsWith(TEXT("Crystal_")))    { Size = FVector(6.f, 6.f, 6.f);  Col = FLinearColor(0.80f, 0.85f, 0.95f); }
		else if (S.StartsWith(TEXT("Hash_")))       { Size = FVector(6.f, 6.f, 4.f);  Col = FLinearColor(0.35f, 0.22f, 0.12f); }
		else if (S.StartsWith(TEXT("Rosin_")))      { Size = FVector(5.f, 5.f, 3.f);  Col = FLinearColor(0.88f, 0.62f, 0.16f); } // amber rosin
		else if (S.StartsWith(TEXT("Bubble_")))     { Size = FVector(6.f, 6.f, 4.f);  Col = FLinearColor(0.74f, 0.62f, 0.40f); } // blonde bubble hash
		else if (S.StartsWith(TEXT("Moonrock_")))   { Size = FVector(7.f, 7.f, 7.f);  Col = FLinearColor(0.18f, 0.28f, 0.16f); } // dark coated bud
		else if (S.StartsWith(TEXT("Oil_")))        { M = Cylinder(); Size = FVector(3.f, 3.f, 7.f); Col = FLinearColor(0.86f, 0.54f, 0.10f); } // amber oil vial
		else if (S.StartsWith(TEXT("Seed_")))       { Size = FVector(2.5f, 2.5f, 2.5f); Col = WeedUI::TagColorForItem(ItemId, 0.5f, 0.65f); } // strain-kleur (matcht het UI-icoon)
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
	// bKeepMeshMaterial = het eigen materiaal van de mesh laten staan (voor echte gescande assets;
	// géén BaseMat-kleur eroverheen).
	inline UStaticMeshComponent* SpawnItemPart(AActor* Owner, USceneComponent* Parent, UStaticMesh* M,
		const FVector& SizeCm, const FVector& LocCm, const FLinearColor& Col, const FRotator& Rot,
		bool bFirstPerson, bool bCollision, bool bKeepMeshMaterial = false)
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
		if (!bKeepMeshMaterial)
		{
			if (UMaterialInterface* BM = BaseMat())
			{
				if (UMaterialInstanceDynamic* MID = C->CreateDynamicMaterialInstance(0, BM)) { MID->SetVectorParameterValue(TEXT("Color"), Col); }
			}
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

	// Laad de geregistreerde placeable-mesh (echte CityBeachStrip-asset of basis-vorm) als één MINI-onderdeel, met
	// behoud van de footprint-proporties (Def.MeshScale). Voor furniture/structures = exacte mini; voor overige
	// placeables zonder eigen branch = nette geproportioneerde vorm i.p.v. een "?"-blokje. False = niet plaatsbaar.
	inline bool BuildRegistryMesh(AActor* Owner, USceneComponent* Parent, FName ItemId, float ScaleMul,
		bool bFirstPerson, bool bCollision)
	{
		FPlaceableDef Def;
		if (!GetPlaceableDef(ItemId, Def) || !Def.MeshPath) { return false; }
		UStaticMesh* M = LoadObject<UStaticMesh>(nullptr, Def.MeshPath);
		if (!M) { return false; }
		// Geplaatste afmeting = natuurlijke bounds * component-schaal; normaliseer de grootste as naar ~12cm.
		const FVector PlacedHalf = M->GetBounds().BoxExtent * Def.MeshScale;
		const float MaxDim = FMath::Max3(PlacedHalf.X, PlacedHalf.Y, PlacedHalf.Z) * 2.f;
		const float Norm = (MaxDim > 1.f) ? (12.f / MaxDim) : 0.12f;
		const FVector CompScale = Def.MeshScale * Norm * ScaleMul; // SpawnItemPart zet schaal = SizeCm/100
		SpawnItemPart(Owner, Parent, M, CompScale * 100.f, FVector::ZeroVector,
			FLinearColor(0.60f, 0.58f, 0.56f), FRotator::ZeroRotator, bFirstPerson, bCollision);
		return true;
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
			// Echte gescande joint-meshes (CC-BY, attributie in Docs/CREDITS.md): gram-tier kiest het
			// model (2g/5g = klein ~13cm, 7g/10g = dik ~17cm), LIGGEND langs X zodat de physics-doos
			// lang-en-plat wordt en de joint altijd op z'n zij settelt.
			UStaticMesh* JM = nullptr; float JScale = 1.f; FRotator JRot = FRotator::ZeroRotator; FVector JOff = FVector::ZeroVector, JTip = FVector::ZeroVector;
			if (GetJointMeshModel(ItemId, JM, JScale, JRot, JOff, JTip))
			{
				// Eigen scan-materiaal behouden (bKeepMeshMaterial): geen BaseMat-kleur eroverheen.
				SpawnItemPart(Owner, Parent, JM, FVector(JScale * 100.f) * ScaleMul, JOff * ScaleMul,
					FLinearColor::White, JRot, bFirstPerson, bCollision, /*bKeepMeshMaterial*/ true);
				// GEEN brandende/gloeiende punt: de speler wil het CLEAN gescande joint-model (niet alsof
				// hij brandt) - alleen de mesh met z'n eigen scan-materiaal. (JTip ongebruikt gelaten.)
				(void)JTip;
			}
			else
			{
				// Fallback (asset mist): het oude procedurele model, nooit een kale kubus.
				P(Cy, FVector(1.3f, 1.3f, 7.3f), FVector(0, 0, 0), FLinearColor(0.96f, 0.95f, 0.90f));    // vloei
				P(Co, FVector(1.3f, 1.3f, 1.9f), FVector(0, 0, 4.5f), FLinearColor(0.90f, 0.88f, 0.80f)); // gedraaide top
				P(Cy, FVector(1.4f, 1.4f, 1.9f), FVector(0, 0, -3.6f), FLinearColor(0.76f, 0.58f, 0.34f)); // filter
				P(Cy, FVector(1.4f, 1.4f, 0.45f), FVector(0, 0, -2.6f), Accent);                           // strain-bandje
				// (geen gloed/hete kern: clean model, ook in de fallback)
			}
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
			// Rechte CILINDER-pot (zoals de geplaatste GrowPlant: Cylinder-body 50x40, geen taps/cone-goblet).
			P(Cy, FVector(10.6f, 10.6f, 9.2f), FVector(0, 0, -0.6f), FLinearColor(0.64f, 0.36f, 0.22f)); // cilinder-body
			P(Cy, FVector(11.4f, 11.4f, 1.8f), FVector(0, 0, 3.9f), FLinearColor(0.58f, 0.32f, 0.19f));  // rand-lip boven
			P(Cy, FVector(9.3f, 9.3f, 1.2f), FVector(0, 0, 3.7f), FLinearColor(0.16f, 0.12f, 0.08f));    // donkere holle binnenkant
			P(Cy, FVector(7.8f, 7.8f, 1.4f), FVector(0, 0, -4.9f), FLinearColor(0.50f, 0.28f, 0.16f));   // voetje onder
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
		else if (S.StartsWith(TEXT("Baked_")))
		{
			// Gebakken/gedecarbte wiet: bruinige geroosterde nug-cluster.
			const FLinearColor B = FMath::Lerp(FLinearColor(0.42f, 0.30f, 0.14f), Accent, 0.25f);
			P(Sp, FVector(6.0f, 6.0f, 5.2f), FVector(0, 0, 0), B);
			P(Sp, FVector(4.2f, 4.2f, 4.0f), FVector(2.4f, 1.0f, 1.2f), B * 1.15f);
			P(Sp, FVector(4.0f, 4.0f, 3.8f), FVector(-2.2f, 1.4f, 1.0f), B * 0.85f);
			P(Sp, FVector(3.6f, 3.6f, 3.4f), FVector(0.4f, -2.2f, 1.6f), B);
			P(Cu, FVector(8.0f, 8.0f, 0.6f), FVector(0, 0, -2.6f), FLinearColor(0.20f, 0.20f, 0.22f)); // bakplaat-bodempje
		}
		else if (S.StartsWith(TEXT("ButterMix_")))
		{
			// Gekookte boter-wiet-mix: geel-groenige klont.
			const FLinearColor MixC = FMath::Lerp(FLinearColor(0.86f, 0.72f, 0.28f), Accent, 0.3f);
			P(Sp, FVector(6.4f, 6.4f, 4.6f), FVector(0, 0, 0), MixC);
			P(Sp, FVector(3.4f, 3.4f, 2.6f), FVector(1.6f, 0.8f, 1.0f), MixC * 0.85f);
			P(Sp, FVector(2.2f, 2.2f, 1.8f), FVector(-1.4f, -0.6f, 1.2f), FLinearColor(0.30f, 0.45f, 0.20f)); // wiet-vlokje
			P(Sp, FVector(1.8f, 1.8f, 1.5f), FVector(0.8f, -1.6f, 1.0f), FLinearColor(0.28f, 0.42f, 0.18f));
		}
		else if (S.StartsWith(TEXT("Edible_")))
		{
			// Eind-cannabutter/edible: bruin blok (boterblok/brownie) met lichtere top.
			const FLinearColor E = FMath::Lerp(FLinearColor(0.45f, 0.30f, 0.15f), Accent, 0.22f);
			P(Cu, FVector(7.0f, 5.0f, 3.6f), FVector(0, 0, 0), E);
			P(Cu, FVector(7.2f, 5.2f, 0.9f), FVector(0, 0, 1.9f), E * 1.25f); // glanzende top
			P(Cu, FVector(1.0f, 5.2f, 3.7f), FVector(-1.8f, 0, 0), E * 0.85f); // snij-lijn
			P(Sp, FVector(0.8f, 0.8f, 0.8f), FVector(1.6f, 1.0f, 2.0f), FLinearColor(0.25f, 0.4f, 0.18f)); // wiet-spikkel
		}
		else if (S == TEXT("Butter"))
		{
			// Pakje boter: geel blok met wit/folie-randje.
			P(Cu, FVector(8.0f, 4.4f, 3.2f), FVector(0, 0, 0), FLinearColor(0.95f, 0.85f, 0.35f));
			P(Cu, FVector(8.2f, 4.6f, 0.8f), FVector(0, 0, 1.7f), FLinearColor(0.98f, 0.92f, 0.55f)); // top
			P(Cu, FVector(8.3f, 4.7f, 1.0f), FVector(0, 0, -1.3f), FLinearColor(0.85f, 0.84f, 0.80f)); // folie-wikkel
		}
		else if (S.StartsWith(TEXT("DryRack_")))
		{
			P(Cu, FVector(1.0f, 2.4f, 12.0f), FVector(-4.7f, 0, 0), FLinearColor(0.40f, 0.30f, 0.20f)); // linker staander
			P(Cu, FVector(1.0f, 2.4f, 12.0f), FVector( 4.7f, 0, 0), FLinearColor(0.40f, 0.30f, 0.20f)); // rechter staander
			P(Cu, FVector(10.0f, 2.4f, 1.0f), FVector(0, 0,  5.5f), FLinearColor(0.40f, 0.30f, 0.20f)); // bovenbalk
			P(Cu, FVector(10.0f, 2.4f, 1.0f), FVector(0, 0, -5.5f), FLinearColor(0.40f, 0.30f, 0.20f)); // onderbalk
			P(Cu, FVector(8.0f, 0.6f, 10.0f), FVector(0, -1.1f, 0), FLinearColor(0.62f, 0.58f, 0.48f)); // achtergaas
			P(Cu, FVector(8.0f, 1.0f, 0.6f), FVector(0, 0.3f,  4.2f), FLinearColor(0.30f, 0.22f, 0.14f)); // hang-roede 1
			P(Cu, FVector(8.0f, 1.0f, 0.6f), FVector(0, 0.3f,  2.4f), FLinearColor(0.30f, 0.22f, 0.14f)); // hang-roede 2
			P(Cu, FVector(8.0f, 1.0f, 0.6f), FVector(0, 0.3f,  0.6f), FLinearColor(0.30f, 0.22f, 0.14f)); // hang-roede 3
			P(Cu, FVector(8.0f, 1.0f, 0.6f), FVector(0, 0.3f, -1.2f), FLinearColor(0.30f, 0.22f, 0.14f)); // hang-roede 4
			P(Cu, FVector(8.0f, 1.0f, 0.6f), FVector(0, 0.3f, -3.0f), FLinearColor(0.30f, 0.22f, 0.14f)); // hang-roede 5
			P(Cu, FVector(4.6f, 1.0f, 2.0f), FVector(0, 0.3f,  3.1f), FLinearColor(0.20f, 0.45f, 0.16f)); // wiet-bos 1
			P(Cu, FVector(4.6f, 1.0f, 2.0f), FVector(0, 0.3f,  1.3f), FLinearColor(0.20f, 0.45f, 0.16f)); // wiet-bos 2
			P(Cu, FVector(4.6f, 1.0f, 2.0f), FVector(0, 0.3f, -0.5f), FLinearColor(0.20f, 0.45f, 0.16f)); // wiet-bos 3
			P(Cu, FVector(4.6f, 1.0f, 2.0f), FVector(0, 0.3f, -2.3f), FLinearColor(0.20f, 0.45f, 0.16f)); // wiet-bos 4
			P(Cu, FVector(4.6f, 1.0f, 2.0f), FVector(0, 0.3f, -4.1f), FLinearColor(0.20f, 0.45f, 0.16f)); // wiet-bos 5
		}
		else if (S.StartsWith(TEXT("Bench_Pack")))
		{
			P(Cu, FVector(13.0f, 8.5f, 1.2f), FVector(0, 0, 5.4f), FLinearColor(0.62f, 0.65f, 0.70f)); // werkblad
			P(Cu, FVector(12.0f, 6.8f, 0.8f), FVector(0, 0, -2.4f), FLinearColor(0.40f, 0.28f, 0.17f)); // onderplank
			P(Cu, FVector(1.1f, 1.1f, 10.6f), FVector( 5.4f,  3.4f, -0.5f), FLinearColor(0.34f, 0.42f, 0.52f)); // poot VR
			P(Cu, FVector(1.1f, 1.1f, 10.6f), FVector( 5.4f, -3.4f, -0.5f), FLinearColor(0.34f, 0.42f, 0.52f)); // poot VL
			P(Cu, FVector(1.1f, 1.1f, 10.6f), FVector(-5.4f,  3.4f, -0.5f), FLinearColor(0.34f, 0.42f, 0.52f)); // poot AR
			P(Cu, FVector(1.1f, 1.1f, 10.6f), FVector(-5.4f, -3.4f, -0.5f), FLinearColor(0.34f, 0.42f, 0.52f)); // poot AL
			P(Cu, FVector(2.6f, 2.0f, 1.4f), FVector(3.0f, 0, 6.7f), FLinearColor(0.15f, 0.16f, 0.18f)); // weegschaal
			P(Cy, FVector(2.0f, 2.0f, 2.6f), FVector(-3.2f, 0, 6.6f), FLinearColor(0.85f, 0.82f, 0.6f), FRotator(0.f, 0.f, 90.f)); // rol zakjes
		}
		else if (S.StartsWith(TEXT("Shelf")) || S.StartsWith(TEXT("Chest")))
		{
			if (S.StartsWith(TEXT("Chest")))
			{
				P(Cu, FVector(13.0f, 8.0f, 6.5f), FVector(0, 0, -1.8f), FLinearColor(0.32f, 0.20f, 0.10f)); // romp
				P(Cu, FVector(13.3f, 8.2f, 3.2f), FVector(0, 0, 2.6f), FLinearColor(0.40f, 0.26f, 0.14f), FRotator(8.f, 0.f, 0.f)); // schuin deksel
				P(Cu, FVector(1.3f, 8.4f, 4.0f), FVector(-3.9f, 0, -1.5f), FLinearColor(0.22f, 0.14f, 0.07f)); // beslagstrip links
				P(Cu, FVector(1.3f, 8.4f, 4.0f), FVector( 3.9f, 0, -1.5f), FLinearColor(0.22f, 0.14f, 0.07f)); // beslagstrip rechts
				P(Cu, FVector(1.8f, 0.8f, 1.4f), FVector(0, 4.1f, -0.5f), FLinearColor(0.70f, 0.60f, 0.20f)); // messing slot
			}
			else
			{
				P(Cu, FVector(0.8f, 7.0f, 13.0f), FVector(-5.6f, 0, 0), FLinearColor(0.45f, 0.30f, 0.18f)); // zijpaneel links
				P(Cu, FVector(0.8f, 7.0f, 13.0f), FVector( 5.6f, 0, 0), FLinearColor(0.45f, 0.30f, 0.18f)); // zijpaneel rechts
				P(Cu, FVector(12.0f, 0.7f, 13.0f), FVector(0, 3.15f, 0), FLinearColor(0.32f, 0.21f, 0.13f)); // achterwand
				P(Cu, FVector(10.4f, 6.4f, 0.7f), FVector(0, 0, -5.0f), FLinearColor(0.56f, 0.38f, 0.23f)); // legbord 1
				P(Cu, FVector(10.4f, 6.4f, 0.7f), FVector(0, 0, -1.7f), FLinearColor(0.56f, 0.38f, 0.23f)); // legbord 2
				P(Cu, FVector(10.4f, 6.4f, 0.7f), FVector(0, 0, 1.6f), FLinearColor(0.56f, 0.38f, 0.23f)); // legbord 3
				P(Cu, FVector(10.4f, 6.4f, 0.7f), FVector(0, 0, 4.9f), FLinearColor(0.56f, 0.38f, 0.23f)); // legbord 4
			}
		}
		else if (S.StartsWith(TEXT("Sink")))
		{
			P(Cu, FVector(9.6f, 6.5f, 10.1f), FVector(0, 0, -2.2f), FLinearColor(0.82f, 0.80f, 0.76f)); // kastje (volle hoogte, zoals geplaatst)
			P(Cu, FVector(0.3f, 6.6f, 5.0f), FVector(0, 0, -2.4f), FLinearColor(0.45f, 0.44f, 0.42f)); // deur-naad
			P(Cu, FVector(10.1f, 6.7f, 0.8f), FVector(0, 0, 1.1f), FLinearColor(0.55f, 0.57f, 0.60f)); // werkblad
			P(Cu, FVector(5.8f, 5.0f, 0.9f), FVector(0, 0, 1.0f), FLinearColor(0.70f, 0.72f, 0.75f)); // rvs bak-rand
			P(Cu, FVector(4.6f, 3.8f, 0.8f), FVector(0, 0, 0.9f), FLinearColor(0.30f, 0.32f, 0.35f)); // bak-opening
			P(Cu, FVector(1.0f, 1.0f, 0.7f), FVector(0, -2.2f, 1.7f), FLinearColor(0.70f, 0.72f, 0.75f)); // kraan-voet
			P(Cy, FVector(0.6f, 0.6f, 3.0f), FVector(0, -2.2f, 3.4f), FLinearColor(0.70f, 0.72f, 0.75f)); // kraan-hals
			P(Cy, FVector(0.55f, 0.55f, 2.2f), FVector(0, -1.1f, 4.7f), FLinearColor(0.70f, 0.72f, 0.75f), FRotator(0, 0, 90)); // kraan-tuit
		}
		else if (S.StartsWith(TEXT("Lamp_Ceiling")))
		{
			P(Cy, FVector(4.8f, 4.8f, 0.9f), FVector(0, 0, 6.5f), FLinearColor(0.05f, 0.05f, 0.07f)); // ophangplaatje (plat schijfje)
			P(Cy, FVector(0.9f, 0.9f, 3.6f), FVector(0, 0, 4.0f), FLinearColor(0.05f, 0.05f, 0.07f)); // dun steeltje
			P(Co, FVector(8.4f, 8.4f, 6.6f), FVector(0, 0, 0.5f), FLinearColor(0.07f, 0.07f, 0.09f), FRotator(180, 0, 0)); // kapje (28:22 verhouding)
			P(Sp, FVector(3.6f, 3.6f, 3.6f), FVector(0, 0, -2.5f), FLinearColor(1.0f, 0.85f, 0.5f)); // warm gloeilampje
		}
		else if (S.StartsWith(TEXT("LightSwitch")))
		{
			P(Cu, FVector(1.2f, 8.5f, 13.0f), FVector(0, 0, 0), FLinearColor(0.90f, 0.90f, 0.88f)); // schakelplaatje (dun in X, breed in Y)
			P(Cu, FVector(2.2f, 4.5f, 6.0f), FVector(1.6f, 0, 0), FLinearColor(0.75f, 0.75f, 0.72f)); // tuimelaar steekt uit in +X
		}
		else if (S == TEXT("Table") || S.StartsWith(TEXT("Furn_CoffeeTable")) || S.StartsWith(TEXT("Furn_Desk")))
		{
			P(Cu, FVector(13.0f, 9.0f, 1.2f), FVector(0, 0, 3.4f), FLinearColor(0.52f, 0.36f, 0.22f)); // tafelblad
			P(Cu, FVector(1.2f, 1.2f, 6.8f), FVector( 5.4f,  3.4f, -0.5f), FLinearColor(0.40f, 0.27f, 0.16f)); // poot
			P(Cu, FVector(1.2f, 1.2f, 6.8f), FVector( 5.4f, -3.4f, -0.5f), FLinearColor(0.40f, 0.27f, 0.16f)); // poot
			P(Cu, FVector(1.2f, 1.2f, 6.8f), FVector(-5.4f,  3.4f, -0.5f), FLinearColor(0.40f, 0.27f, 0.16f)); // poot
			P(Cu, FVector(1.2f, 1.2f, 6.8f), FVector(-5.4f, -3.4f, -0.5f), FLinearColor(0.40f, 0.27f, 0.16f)); // poot
		}
		// === Huiskamer-meubels (basis-vorm placeables) -> herkenbaar mini-model =======================
		else if (S.StartsWith(TEXT("Furn_TVStand")))
		{
			P(Cu, FVector(15.0f, 4.2f, 4.8f), FVector(0, 0, -1.2f), FLinearColor(0.40f, 0.28f, 0.18f));
			P(Cu, FVector(15.2f, 4.4f, 0.5f), FVector(0, 0, 1.9f),  FLinearColor(0.50f, 0.36f, 0.24f));
			P(Cu, FVector(1.0f, 4.3f, 3.6f),  FVector(0, 0, -1.2f), FLinearColor(0.30f, 0.22f, 0.14f));
			P(Cu, FVector(7.0f, 4.3f, 0.4f),  FVector(-3.8f, 0, -1.2f), FLinearColor(0.55f, 0.50f, 0.46f));
			P(Cu, FVector(7.0f, 4.3f, 0.4f),  FVector(3.8f, 0, -1.2f),  FLinearColor(0.55f, 0.50f, 0.46f));
			P(Cy, FVector(1.2f, 1.2f, 0.6f),  FVector(0, 2.2f, -0.8f),  FLinearColor(0.70f, 0.65f, 0.55f));
			P(Cy, FVector(1.2f, 1.2f, 0.6f),  FVector(0, 2.2f, 0.8f),   FLinearColor(0.70f, 0.65f, 0.55f));
		}
		else if (S.StartsWith(TEXT("Furn_Bookshelf")))
		{
			P(Cu, FVector(0.8f, 6.8f, 12.6f), FVector(-4.8f, 0, 0), FLinearColor(0.38f, 0.26f, 0.16f));
			P(Cu, FVector(0.8f, 6.8f, 12.6f), FVector(4.8f, 0, 0),  FLinearColor(0.38f, 0.26f, 0.16f));
			P(Cu, FVector(10.0f, 0.5f, 12.6f), FVector(0, 3.15f, 0), FLinearColor(0.28f, 0.18f, 0.10f));
			P(Cu, FVector(9.6f, 6.4f, 0.5f), FVector(0, 0, 5.2f),  FLinearColor(0.42f, 0.30f, 0.18f));
			P(Cu, FVector(9.6f, 6.4f, 0.5f), FVector(0, 0, 2.2f),  FLinearColor(0.42f, 0.30f, 0.18f));
			P(Cu, FVector(9.6f, 6.4f, 0.5f), FVector(0, 0, -0.8f), FLinearColor(0.42f, 0.30f, 0.18f));
			P(Cu, FVector(9.6f, 6.4f, 0.5f), FVector(0, 0, -3.8f), FLinearColor(0.42f, 0.30f, 0.18f));
			P(Cu, FVector(4.2f, 6.0f, 1.8f), FVector(-2.4f, 0.5f, 4.2f), FLinearColor(0.55f, 0.42f, 0.28f));
		}
		else if (S.StartsWith(TEXT("Furn_Dresser")))
		{
			P(Cu, FVector(10.0f, 5.2f, 9.8f), FVector(0, 0, 0),    FLinearColor(0.38f, 0.26f, 0.16f));
			P(Cu, FVector(10.2f, 5.4f, 0.5f), FVector(0, 0, 4.9f), FLinearColor(0.46f, 0.32f, 0.20f));
			P(Cu, FVector(9.8f, 5.0f, 2.0f), FVector(0, 0, 3.6f),  FLinearColor(0.45f, 0.32f, 0.20f));
			P(Cu, FVector(9.8f, 5.0f, 2.0f), FVector(0, 0, 1.4f),  FLinearColor(0.42f, 0.30f, 0.18f));
			P(Cu, FVector(9.8f, 5.0f, 2.0f), FVector(0, 0, -0.8f), FLinearColor(0.45f, 0.32f, 0.20f));
			P(Cu, FVector(9.8f, 5.0f, 2.0f), FVector(0, 0, -3.0f), FLinearColor(0.42f, 0.30f, 0.18f));
			P(Cy, FVector(0.8f, 0.8f, 0.4f), FVector(3.8f, 2.6f, 3.6f),  FLinearColor(0.75f, 0.60f, 0.30f));
			P(Cy, FVector(0.8f, 0.8f, 0.4f), FVector(3.8f, 2.6f, -0.8f), FLinearColor(0.75f, 0.60f, 0.30f));
		}
		else if (S.StartsWith(TEXT("Furn_Nightstand")))
		{
			P(Cu, FVector(4.2f, 4.2f, 4.8f), FVector(0, 0, -0.2f), FLinearColor(0.38f, 0.26f, 0.16f));
			P(Cu, FVector(4.4f, 4.4f, 0.4f), FVector(0, 0, 2.3f),  FLinearColor(0.46f, 0.32f, 0.20f));
			P(Cu, FVector(4.0f, 4.0f, 1.6f), FVector(0, 0, 0.6f),  FLinearColor(0.42f, 0.30f, 0.18f));
			P(Cy, FVector(0.6f, 0.6f, 0.3f), FVector(1.6f, 2.1f, 0.6f), FLinearColor(0.70f, 0.55f, 0.25f));
			P(Cu, FVector(0.8f, 0.8f, 4.6f), FVector(-1.8f, 2.0f, 0), FLinearColor(0.28f, 0.18f, 0.10f));
			P(Cu, FVector(0.8f, 0.8f, 4.6f), FVector(1.8f, 2.0f, 0),  FLinearColor(0.28f, 0.18f, 0.10f));
		}
		else if (S.StartsWith(TEXT("Furn_Rug")))
		{
			P(Cu, FVector(19.0f, 13.0f, 0.3f), FVector(0, 0, 0),     FLinearColor(0.65f, 0.50f, 0.35f));
			P(Cu, FVector(19.2f, 13.2f, 0.1f), FVector(0, 0, 0.12f), FLinearColor(0.72f, 0.58f, 0.42f));
			P(Cu, FVector(17.0f, 11.0f, 0.2f), FVector(0, 0, -0.06f),FLinearColor(0.60f, 0.45f, 0.30f));
			P(Cu, FVector(9.0f, 5.0f, 0.15f), FVector(3.0f, 2.5f, -0.05f),   FLinearColor(0.58f, 0.42f, 0.28f));
			P(Cu, FVector(8.0f, 4.5f, 0.15f), FVector(-4.0f, -3.0f, -0.05f), FLinearColor(0.62f, 0.48f, 0.32f));
		}
		else if (S.StartsWith(TEXT("Furn_FloorLamp")))
		{
			P(Cy, FVector(1.6f, 1.6f, 7.8f), FVector(0, 0, 2.2f),  FLinearColor(0.22f, 0.22f, 0.25f));
			P(Cy, FVector(4.4f, 4.4f, 0.8f), FVector(0, 0, -3.0f), FLinearColor(0.32f, 0.32f, 0.36f));
			P(Cy, FVector(0.5f, 0.5f, 3.0f), FVector(0, 0, 6.8f),  FLinearColor(0.20f, 0.20f, 0.22f));
			P(Co, FVector(5.2f, 5.2f, 4.2f), FVector(0, 0, 9.5f),  FLinearColor(0.92f, 0.88f, 0.72f));
			P(Sp, FVector(1.2f, 1.2f, 1.2f), FVector(0, 0, 10.6f), FLinearColor(1.0f, 0.95f, 0.50f));
		}
		// === Appliances / opslag =====================================================================
		else if (S == TEXT("Fridge"))
		{
			P(Cu, FVector(9.0f, 7.2f, 16.5f), FVector(0, 0, 0), FLinearColor(0.92f, 0.92f, 0.90f));
			P(Cu, FVector(0.4f, 7.4f, 16.8f), FVector(0, 0, 0), FLinearColor(0.75f, 0.75f, 0.73f));
			P(Cu, FVector(0.8f, 0.8f, 6.0f),  FVector(3.8f, -3.8f, 4.0f), FLinearColor(0.50f, 0.50f, 0.50f));
			P(Cu, FVector(8.6f, 6.8f, 1.5f),  FVector(0, 0, 7.5f),  FLinearColor(0.88f, 0.88f, 0.86f));
			P(Cu, FVector(8.2f, 6.4f, 1.8f),  FVector(0, 0, -7.8f), FLinearColor(0.85f, 0.85f, 0.83f));
		}
		else if (S == TEXT("Mattress"))
		{
			P(Cu, FVector(12.0f, 9.0f, 3.2f), FVector(0, 0, -0.4f), FLinearColor(0.55f, 0.50f, 0.48f));
			P(Sp, FVector(12.4f, 9.4f, 3.6f), FVector(0, 0, -0.2f), FLinearColor(0.58f, 0.53f, 0.50f));
			P(Cu, FVector(4.8f, 3.6f, 1.8f),  FVector(3.2f, 3.2f, 1.8f), FLinearColor(0.72f, 0.68f, 0.65f));
			P(Sp, FVector(5.2f, 4.0f, 2.0f),  FVector(3.2f, 3.2f, 2.0f), FLinearColor(0.75f, 0.70f, 0.67f));
			P(Cu, FVector(11.6f, 8.6f, 0.6f), FVector(0, 0, 1.8f),  FLinearColor(0.42f, 0.38f, 0.35f));
		}
		else if (S == TEXT("Wardrobe"))
		{
			P(Cu, FVector(10.4f, 5.0f, 19.2f), FVector(0, 0, 0), FLinearColor(0.62f, 0.48f, 0.35f));
			P(Cu, FVector(5.0f, 5.2f, 19.4f),  FVector(-2.6f, 0, 0), FLinearColor(0.55f, 0.42f, 0.29f));
			P(Cu, FVector(5.0f, 5.2f, 19.4f),  FVector(2.6f, 0, 0),  FLinearColor(0.55f, 0.42f, 0.29f));
			P(Cu, FVector(0.5f, 5.4f, 19.6f),  FVector(0, 0, 0), FLinearColor(0.45f, 0.35f, 0.22f));
			P(Cy, FVector(0.7f, 0.7f, 1.2f),   FVector(-2.4f, -2.8f, 5.0f), FLinearColor(0.70f, 0.65f, 0.55f), FRotator(0, 0, 90));
			P(Cy, FVector(0.7f, 0.7f, 1.2f),   FVector(2.4f, -2.8f, 5.0f),  FLinearColor(0.70f, 0.65f, 0.55f), FRotator(0, 0, 90));
			P(Cu, FVector(10.8f, 5.0f, 0.8f),  FVector(0, 0, 9.8f), FLinearColor(0.48f, 0.37f, 0.25f));
		}
		// === Grow-gear ===============================================================================
		else if (S.StartsWith(TEXT("Gear_Drainage")))
		{
			P(Cu, FVector(9.0f, 9.0f, 2.0f), FVector(0, 0, 0),    FLinearColor(0.60f, 0.48f, 0.35f));
			P(Cu, FVector(9.6f, 9.6f, 0.6f), FVector(0, 0, 1.0f), FLinearColor(0.50f, 0.40f, 0.28f));
			P(Cu, FVector(8.0f, 8.0f, 1.6f), FVector(0, 0, -0.2f),FLinearColor(0.65f, 0.54f, 0.40f));
			P(Sp, FVector(0.8f, 0.8f, 0.8f), FVector(2.8f, 2.8f, 0.5f),  FLinearColor(0.72f, 0.68f, 0.62f));
			P(Sp, FVector(0.9f, 0.9f, 0.9f), FVector(-2.5f, 3.0f, 0.3f), FLinearColor(0.68f, 0.60f, 0.50f));
			P(Sp, FVector(0.7f, 0.7f, 0.7f), FVector(1.2f, -3.2f, 0.6f), FLinearColor(0.70f, 0.62f, 0.52f));
		}
		else if (S.StartsWith(TEXT("Gear_Insulation")))
		{
			P(Cu, FVector(8.0f, 8.0f, 7.0f), FVector(0, 0, 0), FLinearColor(0.88f, 0.86f, 0.80f));
			P(Cu, FVector(7.0f, 7.0f, 6.0f), FVector(0, 0, 0), FLinearColor(0.92f, 0.91f, 0.88f));
			P(Cu, FVector(6.2f, 6.2f, 5.2f), FVector(0, 0, 0), FLinearColor(0.42f, 0.38f, 0.32f));
			P(Cu, FVector(7.8f, 0.6f, 6.8f), FVector(0, 3.6f, 0), FLinearColor(0.72f, 0.70f, 0.65f));
			P(Cu, FVector(0.6f, 7.8f, 6.8f), FVector(3.8f, 0, 0), FLinearColor(0.72f, 0.70f, 0.65f));
			P(Cu, FVector(1.2f, 1.2f, 0.8f), FVector(-2.2f, -2.2f, 3.2f), FLinearColor(0.35f, 0.30f, 0.25f));
		}
		else if (S.StartsWith(TEXT("Gear_Bloom")))
		{
			P(Cy, FVector(4.0f, 4.0f, 8.0f), FVector(0, 0, -0.5f), FLinearColor(0.15f, 0.18f, 0.25f));
			P(Cy, FVector(4.2f, 4.2f, 0.8f), FVector(0, 0, -1.2f), FLinearColor(0.12f, 0.15f, 0.22f));
			P(Cy, FVector(3.2f, 3.2f, 2.6f), FVector(0, 0, 0.4f),  FLinearColor(0.20f, 0.22f, 0.28f));
			P(Cy, FVector(2.0f, 2.0f, 1.2f), FVector(0, 0, 4.2f),  FLinearColor(0.35f, 0.32f, 0.30f));
			P(Cu, FVector(3.8f, 0.5f, 3.2f), FVector(0, 0, 0),     FLinearColor(0.85f, 0.42f, 0.18f));
		}
		else if (S.StartsWith(TEXT("Gear_Lamp")))
		{
			P(Co, FVector(10.0f, 10.0f, 5.0f), FVector(0, 0, 2.0f), FLinearColor(0.75f, 0.72f, 0.68f));
			P(Cu, FVector(9.0f, 0.9f, 0.9f), FVector(0, -3.5f, -0.5f), FLinearColor(0.55f, 0.25f, 0.75f));
			P(Cu, FVector(9.0f, 0.9f, 0.9f), FVector(0, 3.5f, -0.5f),  FLinearColor(0.55f, 0.25f, 0.75f));
			P(Cu, FVector(9.0f, 0.9f, 0.9f), FVector(0, 0, 0.5f),      FLinearColor(0.95f, 0.90f, 0.88f));
			P(Cu, FVector(1.2f, 1.2f, 1.2f), FVector(0, 0, -3.0f),     FLinearColor(0.40f, 0.35f, 0.32f));
			P(Sp, FVector(0.5f, 0.5f, 0.5f), FVector(3.0f, -2.0f, 0.2f), FLinearColor(1.0f, 0.80f, 0.20f));
		}
		else if (S.StartsWith(TEXT("Gear_Tent")))
		{
			P(Cu, FVector(9.0f, 9.0f, 10.0f), FVector(0, 0, 0), FLinearColor(0.30f, 0.28f, 0.25f));
			P(Cu, FVector(8.4f, 8.4f, 9.4f),  FVector(0, 0, 0), FLinearColor(0.85f, 0.83f, 0.78f));
			P(Cu, FVector(8.8f, 0.5f, 9.8f),  FVector(0, 4.2f, 0),  FLinearColor(0.15f, 0.14f, 0.12f));
			P(Cu, FVector(0.5f, 8.8f, 9.8f),  FVector(-4.2f, 0, 0), FLinearColor(0.15f, 0.14f, 0.12f));
			P(Cu, FVector(9.2f, 9.2f, 0.8f),  FVector(0, 0, 5.0f),  FLinearColor(0.75f, 0.72f, 0.68f));
			P(Sp, FVector(1.0f, 1.0f, 1.0f),  FVector(3.2f, 3.2f, -3.5f), FLinearColor(0.20f, 0.18f, 0.15f));
		}
		else if (S.StartsWith(TEXT("Gear_Water")))
		{
			P(Cy, FVector(6.0f, 6.0f, 8.0f), FVector(0, 0, 0), FLinearColor(0.18f, 0.32f, 0.52f));
			P(Cy, FVector(6.4f, 6.4f, 0.8f), FVector(0, 0, 4.2f), FLinearColor(0.12f, 0.25f, 0.42f));
			P(Cu, FVector(0.6f, 0.6f, 4.0f), FVector(3.0f, 3.0f, 2.0f),  FLinearColor(0.28f, 0.26f, 0.24f));
			P(Cu, FVector(0.6f, 0.6f, 3.5f), FVector(-3.5f, 2.5f, 1.5f), FLinearColor(0.28f, 0.26f, 0.24f));
			P(Cu, FVector(0.5f, 0.5f, 1.2f), FVector(0, -3.2f, 3.8f),    FLinearColor(0.32f, 0.32f, 0.30f));
		}
		// === Losse dry/proc upgrade-gear =============================================================
		else if (S == TEXT("DryUp_Fan") || S == TEXT("DryUp_FanSmall"))
		{
			// Rechtopstaande ventilator: voet op de vloer (Z=0), staander omhoog, kop met blaadjes -> niet meer in de grond.
			P(Cu, FVector(5.0f, 4.0f, 0.6f), FVector(0, 0, 0.3f),    FLinearColor(0.22f, 0.20f, 0.18f));            // voet
			P(Cy, FVector(0.8f, 0.8f, 6.0f), FVector(0, 0, 3.5f),    FLinearColor(0.30f, 0.28f, 0.26f));            // staander
			P(Cy, FVector(6.0f, 6.0f, 2.0f), FVector(0, 2.0f, 7.0f), FLinearColor(0.28f, 0.26f, 0.24f), FRotator(90, 0, 0)); // kooi, rechtop
			P(Cu, FVector(4.8f, 0.6f, 0.5f), FVector(0, 2.6f, 7.0f), FLinearColor(0.32f, 0.30f, 0.28f), FRotator(90, 0, 0)); // blad
			P(Cu, FVector(0.5f, 0.6f, 4.8f), FVector(0, 2.6f, 7.0f), FLinearColor(0.32f, 0.30f, 0.28f), FRotator(90, 0, 0)); // blad
			P(Sp, FVector(0.5f, 0.5f, 0.5f), FVector(0, 2.9f, 7.0f), FLinearColor(0.15f, 0.15f, 0.18f));          // naaf
		}
		else if (S == TEXT("DryUp_Seal"))
		{
			P(Cu, FVector(5.0f, 5.0f, 4.0f), FVector(0, 0, 0), FLinearColor(0.52f, 0.50f, 0.48f));
			P(Cu, FVector(5.4f, 5.4f, 0.6f), FVector(0, 0, 2.2f), FLinearColor(0.42f, 0.40f, 0.38f));
			P(Cu, FVector(4.4f, 4.4f, 3.6f), FVector(0, 0, -0.2f),FLinearColor(0.35f, 0.33f, 0.30f));
			P(Cu, FVector(0.8f, 5.2f, 0.5f), FVector(2.2f, 0, 1.8f), FLinearColor(0.68f, 0.18f, 0.18f));
			P(Cu, FVector(1.4f, 1.4f, 0.8f), FVector(-1.8f, -1.8f, 1.6f), FLinearColor(0.22f, 0.20f, 0.18f));
		}
		else if (S == TEXT("ProcUp_Motor"))
		{
			P(Cu, FVector(6.0f, 6.0f, 6.0f), FVector(0, 0, 0), FLinearColor(0.35f, 0.32f, 0.30f));
			P(Cu, FVector(5.0f, 5.0f, 5.0f), FVector(0, 0, 0), FLinearColor(0.28f, 0.26f, 0.24f));
			P(Cy, FVector(3.0f, 3.0f, 6.2f), FVector(0, 0, 0.5f), FLinearColor(0.48f, 0.45f, 0.42f));
			P(Cy, FVector(0.8f, 0.8f, 2.0f), FVector(0, 0, -3.0f),FLinearColor(0.18f, 0.16f, 0.14f));
			P(Cu, FVector(1.2f, 1.2f, 1.2f), FVector(2.0f, -2.0f, 2.0f), FLinearColor(0.15f, 0.15f, 0.18f));
		}
		else if (S == TEXT("ProcUp_Yield"))
		{
			P(Cy, FVector(4.2f, 4.2f, 9.0f), FVector(0, 0, 0), FLinearColor(0.25f, 0.28f, 0.32f));
			P(Cy, FVector(4.6f, 4.6f, 0.8f), FVector(0, 0, 4.8f), FLinearColor(0.35f, 0.38f, 0.42f));
			P(Cy, FVector(3.8f, 3.8f, 0.6f), FVector(0, 0, -4.6f),FLinearColor(0.18f, 0.20f, 0.24f));
			P(Cu, FVector(4.4f, 0.4f, 8.8f), FVector(0, 2.0f, 0), FLinearColor(0.15f, 0.18f, 0.22f));
			P(Cu, FVector(0.5f, 0.5f, 2.0f), FVector(0, 0, 6.0f), FLinearColor(0.42f, 0.45f, 0.50f));
		}
		else if (S == TEXT("ProcUp_Purity"))
		{
			// Zuiverings-spoel: koperkleurige coil op een sokkel (onderscheidt zich van motor/filter).
			P(Cy, FVector(3.6f, 3.6f, 1.0f), FVector(0, 0, -4.2f), FLinearColor(0.20f, 0.22f, 0.24f));
			P(Cy, FVector(2.2f, 2.2f, 8.4f), FVector(0, 0, 0.4f),  FLinearColor(0.72f, 0.48f, 0.22f));
			P(Cy, FVector(2.9f, 2.9f, 0.7f), FVector(0, 0, 2.6f),  FLinearColor(0.85f, 0.60f, 0.28f));
			P(Cy, FVector(2.9f, 2.9f, 0.7f), FVector(0, 0, -1.0f), FLinearColor(0.85f, 0.60f, 0.28f));
			P(Cu, FVector(0.5f, 0.5f, 2.0f), FVector(0, 0, 5.6f),  FLinearColor(0.40f, 0.44f, 0.48f));
		}
		// === Processor-machines (gegate op bIsProcessor zodat product-Oil_/Rosin_ niet hier matchen) ===
		else if (FPlaceableDef MDef; GetPlaceableDef(ItemId, MDef) && MDef.bIsProcessor)
		{
			if (S.StartsWith(TEXT("Fridge_")) || S.StartsWith(TEXT("Iso_")))
			{
				P(Cu, FVector(8.0f, 7.0f, 9.5f), FVector(0, 0, 0.5f), FLinearColor(0.88f, 0.88f, 0.90f));
				P(Cu, FVector(7.0f, 0.5f, 8.0f), FVector(0, -3.5f, 0.8f), FLinearColor(0.20f, 0.20f, 0.22f));
				P(Cu, FVector(7.2f, 0.7f, 7.8f), FVector(0, -3.6f, 0.8f), FLinearColor(0.75f, 0.75f, 0.78f));
				P(Cy, FVector(0.5f, 0.5f, 1.8f), FVector(3.4f, -3.2f, 1.0f), FLinearColor(0.65f, 0.65f, 0.68f));
				P(Cu, FVector(6.8f, 0.3f, 1.0f), FVector(0, -2.8f, 4.0f), FLinearColor(0.55f, 0.58f, 0.62f));
				P(Cu, FVector(6.8f, 0.3f, 1.0f), FVector(0, -2.8f, 2.2f), FLinearColor(0.55f, 0.58f, 0.62f));
				P(Cu, FVector(6.8f, 0.3f, 1.0f), FVector(0, -2.8f, 0.4f), FLinearColor(0.55f, 0.58f, 0.62f));
			}
			else if (S.StartsWith(TEXT("Oven_")) || S.StartsWith(TEXT("Pan_")))
			{
				P(Cu, FVector(9.0f, 7.0f, 8.5f), FVector(0, -0.5f, -1.0f), FLinearColor(0.32f, 0.32f, 0.34f));
				P(Cu, FVector(7.4f, 0.9f, 5.0f), FVector(0, -3.6f, 0.5f),  FLinearColor(0.15f, 0.15f, 0.18f));
				P(Cu, FVector(7.6f, 1.1f, 5.2f), FVector(0, -3.65f, 0.5f), FLinearColor(0.22f, 0.22f, 0.26f));
				P(Cu, FVector(8.4f, 8.0f, 1.4f), FVector(0, 0, 4.8f),      FLinearColor(0.65f, 0.62f, 0.50f));
				P(Cy, FVector(0.7f, 0.7f, 0.5f), FVector(2.2f, 2.2f, 5.2f),   FLinearColor(0.20f, 0.20f, 0.22f));
				P(Cy, FVector(0.7f, 0.7f, 0.5f), FVector(-2.2f, 2.2f, 5.2f),  FLinearColor(0.20f, 0.20f, 0.22f));
				P(Sp, FVector(0.8f, 0.8f, 0.8f), FVector(3.0f, 2.8f, 5.8f),   FLinearColor(1.0f, 0.4f, 0.2f));
			}
			else // Mesh_/Press_/Rosin_/Oil_/Moon_ -> tafel-pers
			{
				P(Cu, FVector(10.0f, 8.0f, 2.2f), FVector(0, 0, -4.8f), FLinearColor(0.35f, 0.35f, 0.35f));
				P(Cu, FVector(9.0f, 7.2f, 2.0f),  FVector(0, 0, 4.0f),  FLinearColor(0.45f, 0.45f, 0.48f));
				P(Cy, FVector(1.2f, 1.2f, 7.2f),  FVector(0, 0, 1.2f),  FLinearColor(0.28f, 0.28f, 0.30f));
				P(Cu, FVector(9.4f, 7.6f, 1.8f),  FVector(0, 0, 5.4f),  FLinearColor(0.70f, 0.70f, 0.72f));
				P(Cy, FVector(0.8f, 0.8f, 1.0f),  FVector(3.5f, 2.5f, 5.2f),  FLinearColor(0.85f, 0.35f, 0.15f));
				P(Cy, FVector(0.8f, 0.8f, 1.0f),  FVector(-3.5f, 2.5f, 5.2f), FLinearColor(0.85f, 0.35f, 0.15f));
				P(Cu, FVector(8.0f, 0.6f, 3.0f),  FVector(0, 3.8f, 0.5f), FLinearColor(0.50f, 0.50f, 0.52f));
			}
		}
		// === Concentraat-producten (waren ook "?"-blokjes bij droppen) ===============================
		else if (S.StartsWith(TEXT("Oil_")))
		{
			P(Cy, FVector(3.4f, 3.4f, 7.2f), FVector(0, 0, -0.4f), FLinearColor(0.88f, 0.58f, 0.12f)); // amber-olie vial
			P(Cy, FVector(2.8f, 2.8f, 4.0f), FVector(0, 0, -1.4f), FLinearColor(0.92f, 0.64f, 0.16f)); // olie binnenin
			P(Cy, FVector(3.6f, 3.6f, 0.9f), FVector(0, 0, 3.4f),  FLinearColor(0.20f, 0.18f, 0.12f)); // dop
			P(Cy, FVector(1.0f, 1.0f, 3.0f), FVector(0, 0, 5.2f),  FLinearColor(0.55f, 0.40f, 0.22f)); // druppelaar
		}
		else if (S.StartsWith(TEXT("Rosin_")))
		{
			const FLinearColor R = FMath::Lerp(FLinearColor(0.88f, 0.60f, 0.14f), Accent, 0.16f);
			P(Sp, FVector(5.4f, 5.4f, 2.8f), FVector(0, 0, 0.4f), R);                       // amber rosin-blob
			P(Sp, FVector(3.2f, 3.2f, 2.0f), FVector(1.8f, 1.0f, 0.8f), R * 1.15f);
			P(Sp, FVector(2.6f, 2.6f, 1.7f), FVector(-1.7f, 0.8f, 0.6f), R * 0.9f);
			P(Cu, FVector(7.4f, 7.4f, 0.4f), FVector(0, 0, -1.4f), FLinearColor(0.90f, 0.88f, 0.80f)); // perkament
		}
		else if (S.StartsWith(TEXT("Bubble_")))
		{
			const FLinearColor B2 = FMath::Lerp(FLinearColor(0.74f, 0.62f, 0.40f), Accent, 0.14f);
			P(Sp, FVector(5.6f, 5.6f, 4.0f), FVector(0, 0, 0), B2);                          // blonde bubble-hash bol
			P(Sp, FVector(3.4f, 3.4f, 2.6f), FVector(1.8f, 1.2f, 1.0f), B2 * 1.12f);
			P(Sp, FVector(3.0f, 3.0f, 2.2f), FVector(-1.7f, 0.9f, 0.7f), B2 * 0.88f);
			P(Sp, FVector(2.4f, 2.4f, 1.8f), FVector(0.4f, -1.8f, 1.2f), B2);
		}
		else if (S.StartsWith(TEXT("Moonrock_")))
		{
			const FLinearColor M2 = FMath::Lerp(FLinearColor(0.16f, 0.24f, 0.14f), Accent, 0.20f);
			P(Sp, FVector(6.2f, 6.2f, 5.6f), FVector(0, 0, 0), M2);                          // donkere gecoate nug
			P(Sp, FVector(4.2f, 4.2f, 4.0f), FVector(2.2f, 1.0f, 1.0f), M2 * 1.2f);
			P(Sp, FVector(3.8f, 3.8f, 3.6f), FVector(-2.0f, 1.2f, 0.8f), M2 * 0.85f);
			P(Sp, FVector(0.7f, 0.7f, 0.7f), FVector(1.5f, 1.0f, 2.6f), FLinearColor(0.85f, 0.86f, 0.92f)); // kief-glinster
		}
		else
		{
			// Andere placeable (furniture/structure/processor/...): toon z'n geregistreerde mesh als mini i.p.v. een "?".
			if (!BuildRegistryMesh(Owner, Parent, ItemId, ScaleMul, bFirstPerson, bCollision))
			{
				P(Cu, FVector(6.5f, 6.5f, 6.5f), FVector(0, 0, 0), FLinearColor(0.60f, 0.60f, 0.65f));
				P(Cu, FVector(7.0f, 1.0f, 7.0f), FVector(0, 0, 0), FLinearColor(0.50f, 0.50f, 0.55f), FRotator(0, 0, 45)); // "?"-markering
			}
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
