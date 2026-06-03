#include "World/CityGenerator.h"

#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/PlayerStart.h"
#include "Engine/World.h"
#include "EngineUtils.h"

namespace
{
	// Kleine deterministische hash (geen Math::Rand!) zodat host + clients exact dezelfde stad bouwen.
	uint32 CityHash(int32 a, int32 b)
	{
		uint32 x = (uint32)(a * 73856093) ^ (uint32)(b * 19349663);
		x ^= x >> 13; x *= 0x85ebca6bu; x ^= x >> 16;
		return x;
	}
}

ACityGenerator::ACityGenerator()
{
	PrimaryActorTick.bCanEverTick = false;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
}

void ACityGenerator::BeginPlay()
{
	Super::BeginPlay();
	BuildCity();
}

UStaticMeshComponent* ACityGenerator::AddBox(UStaticMesh* MeshAsset, const FVector& CenterWorld,
	const FVector& SizeCm, const FLinearColor& Color, bool bCollides)
{
	if (!MeshAsset) { return nullptr; }
	UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
	C->SetupAttachment(Root);
	C->RegisterComponent();
	C->SetStaticMesh(MeshAsset);
	C->SetWorldLocation(CenterWorld);
	C->SetWorldScale3D(SizeCm / 100.f); // basis-kubus = 100cm
	C->SetCollisionEnabled(bCollides ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
	C->SetMobility(EComponentMobility::Movable);
	if (UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial")))
	{
		if (UMaterialInstanceDynamic* MID = C->CreateDynamicMaterialInstance(0, Base))
		{
			MID->SetVectorParameterValue(TEXT("Color"), Color);
		}
	}
	return C;
}

void ACityGenerator::BuildCity()
{
	if (bBuilt) { return; }
	UWorld* W = GetWorld();
	if (!W) { return; }
	bBuilt = true;

	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!Cube) { return; }

	// Referentie-midden: de PlayerStart (anders deze actor). Grond-hoogte eronder zoeken.
	FVector Center = GetActorLocation();
	for (TActorIterator<APlayerStart> It(W); It; ++It) { Center = It->GetActorLocation(); break; }
	{
		FHitResult Hit;
		const FVector S = Center + FVector(0.f, 0.f, 500.f);
		const FVector E = Center - FVector(0.f, 0.f, 4000.f);
		FCollisionQueryParams Q(FName(TEXT("CityGround")), false, this);
		GroundZ = W->LineTraceSingleByChannel(Hit, S, E, ECC_WorldStatic, Q) ? Hit.ImpactPoint.Z : (Center.Z - 90.f);
	}

	const float Pitch = BlockSize + RoadWidth; // hart-op-hart afstand tussen blokken
	const int32 R = FMath::Clamp(GridRadius, 1, 8);
	const float Span = (2 * R + 1) * Pitch;

	// Kleurenpaletten.
	const FLinearColor Asphalt(0.06f, 0.06f, 0.07f);
	const FLinearColor Sidewalk(0.42f, 0.42f, 0.45f);
	const FLinearColor Curb(0.30f, 0.30f, 0.33f);
	const FLinearColor Line(0.75f, 0.72f, 0.55f); // gele middenstreep
	const FLinearColor Facades[] = {
		FLinearColor(0.62f, 0.40f, 0.30f), // baksteen
		FLinearColor(0.74f, 0.70f, 0.62f), // beige
		FLinearColor(0.50f, 0.55f, 0.60f), // grijsblauw
		FLinearColor(0.68f, 0.58f, 0.42f), // zandsteen
		FLinearColor(0.55f, 0.42f, 0.40f), // oudroze baksteen
		FLinearColor(0.45f, 0.50f, 0.46f), // groengrijs
	};

	// 1) Eén grote asfaltvloer onder de hele stad (net onder de grond zodat bestaande vloer wint waar die is).
	AddBox(Cube, FVector(Center.X, Center.Y, GroundZ - 3.f), FVector(Span, Span, 6.f), Asphalt, true);

	// 2) Gele middenstrepen op de straten (cosmetisch, geen collision).
	for (int32 k = -R; k <= R; ++k)
	{
		const float P = k * Pitch + Pitch * 0.5f; // midden van de straat tussen blok k en k+1
		if (k == R) { continue; }
		// horizontaal + verticaal
		AddBox(Cube, FVector(Center.X + P, Center.Y, GroundZ + 1.f), FVector(8.f, Span, 2.f), Line, false);
		AddBox(Cube, FVector(Center.X, Center.Y + P, GroundZ + 1.f), FVector(Span, 8.f, 2.f), Line, false);
	}

	// 3) Per blok: stoep (verhoogd) + gebouw. Centrale blokken open laten voor de bestaande shop/straat.
	const int32 Open = FMath::Max(0, OpenPlazaRadius);
	for (int32 i = -R; i <= R; ++i)
	{
		for (int32 j = -R; j <= R; ++j)
		{
			if (FMath::Max(FMath::Abs(i), FMath::Abs(j)) <= Open) { continue; } // open plein in het midden

			const float CX = Center.X + i * Pitch;
			const float CY = Center.Y + j * Pitch;

			// Stoep: verhoogd plateau ter grootte van het blok (de straten ertussen blijven asfalt).
			AddBox(Cube, FVector(CX, CY, GroundZ + CurbHeight * 0.5f), FVector(BlockSize, BlockSize, CurbHeight), Sidewalk, true);
			// Donkere stoeprand-rand eromheen (subtiel) — dun randje net binnen de blokrand.
			AddBox(Cube, FVector(CX, CY, GroundZ + CurbHeight * 0.5f + 1.f), FVector(BlockSize - 6.f, BlockSize - 6.f, CurbHeight + 2.f), Sidewalk * 1.08f, false);

			// Gebouw op het blok (binnen de stoep). Hoogte deterministisch uit de hash.
			const uint32 H = CityHash(i, j);
			const int32 Floors = 2 + (int32)(H % 6);          // 2..7 verdiepingen
			const float FloorH = 330.f;
			const float BH = Floors * FloorH;
			const float Foot = BlockSize - 2.f * SidewalkWidth; // gevel binnen de stoep
			const FLinearColor Body = Facades[H % UE_ARRAY_COUNT(Facades)];
			const float TopZ = GroundZ + CurbHeight;            // gebouw staat op de stoep

			// Romp.
			AddBox(Cube, FVector(CX, CY, TopZ + BH * 0.5f), FVector(Foot, Foot, BH), Body, true);
			// Dakrand (iets breder, donker).
			AddBox(Cube, FVector(CX, CY, TopZ + BH + 8.f), FVector(Foot + 14.f, Foot + 14.f, 16.f), Body * 0.6f, false);
			// Begane grond (winkelpui) iets donkerder + breder zodat het leest als een plint.
			AddBox(Cube, FVector(CX, CY, TopZ + FloorH * 0.5f), FVector(Foot + 8.f, Foot + 8.f, FloorH), Body * 0.5f, false);

			// Raamstroken per verdieping op de 4 gevels (dun, glas-blauw), cosmetisch.
			const FLinearColor Glass(0.30f, 0.45f, 0.55f);
			const float HalfF = Foot * 0.5f;
			for (int32 f = 1; f < Floors; ++f)
			{
				const float Z = TopZ + f * FloorH + FloorH * 0.5f;
				const float WinW = Foot * 0.8f;
				const float WinH = FloorH * 0.5f;
				AddBox(Cube, FVector(CX, CY + HalfF + 2.f, Z), FVector(WinW, 4.f, WinH), Glass, false);
				AddBox(Cube, FVector(CX, CY - HalfF - 2.f, Z), FVector(WinW, 4.f, WinH), Glass, false);
				AddBox(Cube, FVector(CX + HalfF + 2.f, CY, Z), FVector(4.f, WinW, WinH), Glass, false);
				AddBox(Cube, FVector(CX - HalfF - 2.f, CY, Z), FVector(4.f, WinW, WinH), Glass, false);
			}
		}
	}
}
