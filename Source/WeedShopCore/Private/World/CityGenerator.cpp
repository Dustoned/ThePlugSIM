#include "World/CityGenerator.h"

#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/LightComponent.h"
#include "Components/TextRenderComponent.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/PlayerStart.h"
#include "World/StoreCounter.h"
#include "World/CityDoor.h"
#include "World/CityElevator.h"
#include "Game/WeedShopGameState.h"
#include "World/DayCycleComponent.h"
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

	// Gevel-kleurenpalet (gedeeld door flats en rijtjeshuizen).
	const FLinearColor& CityFacade(uint32 i)
	{
		static const FLinearColor Pal[] = {
			FLinearColor(0.62f, 0.40f, 0.30f), // baksteen
			FLinearColor(0.74f, 0.70f, 0.62f), // beige
			FLinearColor(0.50f, 0.55f, 0.60f), // grijsblauw
			FLinearColor(0.68f, 0.58f, 0.42f), // zandsteen
			FLinearColor(0.55f, 0.42f, 0.40f), // oudroze baksteen
			FLinearColor(0.45f, 0.50f, 0.46f), // groengrijs
		};
		return Pal[i % UE_ARRAY_COUNT(Pal)];
	}
}

ACityGenerator::ACityGenerator()
{
	PrimaryActorTick.bCanEverTick = true;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
}

void ACityGenerator::BeginPlay()
{
	Super::BeginPlay();
	BuildCity();
}

void ACityGenerator::AddCityLamp(const FVector& BaseWorld)
{
	UStaticMesh* Cyl = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	UStaticMesh* Cone = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cone.Cone"));
	if (!Cyl) { return; }
	const float PoleH = 470.f;
	const FLinearColor Metal(0.07f, 0.08f, 0.10f);
	AddBox(Cyl, BaseWorld + FVector(0.f, 0.f, 7.f), FVector(36.f, 36.f, 14.f), Metal, false);             // voet
	AddBox(Cyl, BaseWorld + FVector(0.f, 0.f, PoleH * 0.5f), FVector(11.f, 11.f, PoleH), Metal, false);   // paal
	// Lantaarn-kapje (kegel, punt omhoog) + dakje.
	if (Cone) { AddBox(Cone, BaseWorld + FVector(0.f, 0.f, PoleH + 18.f), FVector(52.f, 52.f, 42.f), Metal, false, FRotator(180.f, 0.f, 0.f)); }
	if (Sphere) { AddBox(Sphere, BaseWorld + FVector(0.f, 0.f, PoleH + 42.f), FVector(14.f, 14.f, 12.f), Metal, false); }
	// Gloeiende lampbol in het kapje (kleurt warmgeel als 'ie aan is).
	if (Sphere)
	{
		UStaticMeshComponent* Head = AddBox(Sphere, BaseWorld + FVector(0.f, 0.f, PoleH + 4.f), FVector(26.f, 26.f, 24.f), FLinearColor(0.2f, 0.2f, 0.22f), false);
		if (Head)
		{
			if (UMaterialInstanceDynamic* M = Cast<UMaterialInstanceDynamic>(Head->GetMaterial(0))) { LampHeadMats.Add(M); }
		}
	}
	// Warme SPOTLIGHT recht naar beneden (kegel) -> lichtpoel op de stoep/straat i.p.v. de muur ernaast.
	USpotLightComponent* SL = NewObject<USpotLightComponent>(this);
	SL->SetupAttachment(Root);
	SL->RegisterComponent();
	SL->SetWorldLocation(BaseWorld + FVector(0.f, 0.f, PoleH - 6.f));
	SL->SetWorldRotation(FRotator(-90.f, 0.f, 0.f)); // recht omlaag
	SL->SetMobility(EComponentMobility::Movable);
	SL->SetAttenuationRadius(1200.f);
	SL->SetInnerConeAngle(28.f);
	SL->SetOuterConeAngle(52.f);
	SL->SetLightColor(FLinearColor(1.f, 0.82f, 0.5f));
	SL->SetIntensity(0.f); // tick zet 'm 's avonds aan
	SL->SetCastShadows(false);
	LampLights.Add(SL);
}

void ACityGenerator::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (LampLights.Num() == 0) { return; }
	LampTickAccum += DeltaSeconds;
	if (LampTickAccum < 0.5f) { return; } // niet elke frame
	LampTickAccum = 0.f;

	const AWeedShopGameState* GS = GetWorld() ? GetWorld()->GetGameState<AWeedShopGameState>() : nullptr;
	const UDayCycleComponent* DC = GS ? GS->GetDayCycle() : nullptr;
	if (!DC) { return; }
	const float Hour = DC->GetClockHour();
	const int32 WantOn = (Hour < 8.f || Hour >= 19.f) ? 1 : 0;
	if (WantOn == bLampsOn) { return; }
	bLampsOn = WantOn;
	for (ULightComponent* PL : LampLights) { if (PL) { PL->SetIntensity(WantOn ? 22000.f : 0.f); } }
	for (UMaterialInstanceDynamic* M : LampHeadMats)
	{
		if (M) { M->SetVectorParameterValue(TEXT("Color"), WantOn ? FLinearColor(1.f, 0.86f, 0.5f) : FLinearColor(0.2f, 0.2f, 0.22f)); }
	}
}

UStaticMeshComponent* ACityGenerator::AddBox(UStaticMesh* MeshAsset, const FVector& CenterWorld,
	const FVector& SizeCm, const FLinearColor& Color, bool bCollides, const FRotator& Rot)
{
	if (!MeshAsset) { return nullptr; }
	UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
	C->SetupAttachment(Root);
	C->RegisterComponent();
	C->SetStaticMesh(MeshAsset);
	C->SetWorldLocation(CenterWorld);
	C->SetWorldRotation(Rot);
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

void ACityGenerator::AddGableRoof(const FVector& TopCenter, float Width, float Depth, float RidgeH, bool bRidgeAlongX, const FLinearColor& Color)
{
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!Cube) { return; }
	const float Thick = 12.f;
	if (bRidgeAlongX)
	{
		const float Half = Depth * 0.5f;
		const float L = FMath::Sqrt(Half * Half + RidgeH * RidgeH) + 8.f;
		const float Ang = FMath::RadiansToDegrees(FMath::Atan2(RidgeH, Half));
		AddBox(Cube, FVector(TopCenter.X, TopCenter.Y - Depth * 0.25f, TopCenter.Z + RidgeH * 0.5f), FVector(Width, L, Thick), Color, true, FRotator(0.f, 0.f, -Ang));
		AddBox(Cube, FVector(TopCenter.X, TopCenter.Y + Depth * 0.25f, TopCenter.Z + RidgeH * 0.5f), FVector(Width, L, Thick), Color, true, FRotator(0.f, 0.f,  Ang));
	}
	else
	{
		const float Half = Width * 0.5f;
		const float L = FMath::Sqrt(Half * Half + RidgeH * RidgeH) + 8.f;
		const float Ang = FMath::RadiansToDegrees(FMath::Atan2(RidgeH, Half));
		AddBox(Cube, FVector(TopCenter.X - Width * 0.25f, TopCenter.Y, TopCenter.Z + RidgeH * 0.5f), FVector(L, Depth, Thick), Color, true, FRotator( Ang, 0.f, 0.f));
		AddBox(Cube, FVector(TopCenter.X + Width * 0.25f, TopCenter.Y, TopCenter.Z + RidgeH * 0.5f), FVector(L, Depth, Thick), Color, true, FRotator(-Ang, 0.f, 0.f));
	}
}

void ACityGenerator::AddSignText(const FVector& WorldLoc, int32 DirX, int32 DirY, const FString& Text, const FLinearColor& Color, float Size)
{
	UTextRenderComponent* T = NewObject<UTextRenderComponent>(this);
	T->SetupAttachment(Root);
	T->RegisterComponent();
	T->SetWorldLocation(WorldLoc);
	// Tekst-front naar de straat richten (de speler staat aan de N-kant en kijkt naar het gebouw).
	const float Yaw = FMath::RadiansToDegrees(FMath::Atan2((float)DirY, (float)DirX));
	T->SetWorldRotation(FRotator(0.f, Yaw, 0.f));
	T->SetText(FText::FromString(Text));
	T->SetHorizontalAlignment(EHTA_Center);
	T->SetVerticalAlignment(EVRTA_TextCenter);
	T->SetWorldSize(Size);
	T->SetTextRenderColor(Color.ToFColor(true));
	T->SetCastShadow(false);
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
		// Oude wereld is verwijderd -> meestal geen vloer-hit; val terug op het wereld-grondvlak (z=0)
		// zodat de asfaltvloer altijd op een vaste hoogte ligt en je netjes landt.
		GroundZ = W->LineTraceSingleByChannel(Hit, S, E, ECC_WorldStatic, Q) ? Hit.ImpactPoint.Z : 0.f;
	}

	const float Pitch = BlockSize + RoadWidth; // hart-op-hart afstand tussen blokken
	const int32 R = FMath::Clamp(GridRadius, 1, 8);
	const float Span = (2 * R + 1) * Pitch;

	// Kleurenpaletten.
	const FLinearColor Asphalt(0.06f, 0.06f, 0.07f);
	const FLinearColor Sidewalk(0.42f, 0.42f, 0.45f);
	const FLinearColor Curb(0.30f, 0.30f, 0.33f);
	const FLinearColor Line(0.75f, 0.72f, 0.55f); // gele middenstreep

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

			const float TopZ = GroundZ + CurbHeight; // gebouw/winkel staat op de stoep

			// Straatlantaarns op de stoephoeken: op de checkerboard-blokken alle 4 de hoeken (de hoeken
			// worden met de buren gedeeld -> vrijwel elke straathoek krijgt licht, zonder te veel lampen).
			if (((i + j) & 1) == 0)
			{
				const float Off = BlockSize * 0.5f - 45.f;
				AddCityLamp(FVector(CX + Off, CY + Off, TopZ));
				AddCityLamp(FVector(CX + Off, CY - Off, TopZ));
				AddCityLamp(FVector(CX - Off, CY + Off, TopZ));
				AddCityLamp(FVector(CX - Off, CY - Off, TopZ));
			}

			// Landmark-blokken: 4 winkels + gas station op de binnenring, appartementen op de hoeken.
			// Deur wijst richting het midden (de straat). Voorkeur: X-as als beide assen meedoen.
			int32 ddx = (i > 0) ? -1 : (i < 0 ? 1 : 0);
			int32 ddy = (j > 0) ? -1 : (j < 0 ? 1 : 0);
			if (ddx != 0) { ddy = 0; }

			EShopKind Kind = EShopKind::Apartment;
			bool bLandmark = false;
			if (i == 1 && j == 0)       { Kind = EShopKind::Grow;       bLandmark = true; }
			else if (i == -1 && j == 0) { Kind = EShopKind::Furniture;  bLandmark = true; }
			else if (i == 0 && j == 1)  { Kind = EShopKind::Supplies;   bLandmark = true; }
			else if (i == 0 && j == -1) { Kind = EShopKind::GasStation; bLandmark = true; }
			else if (FMath::Abs(i) == 1 && FMath::Abs(j) == 1) { Kind = EShopKind::Apartment; bLandmark = true; }

			if (bLandmark)
			{
				const uint32 HA = CityHash(i * 7 + 3, j * 13 + 5);
				const float Foot = BlockSize - 2.f * SidewalkWidth;
				const float Height = (Kind == EShopKind::Apartment) ? (3 + (int32)(HA % 4)) * 330.f : 380.f;
				FLinearColor Body, Sign;
				switch (Kind)
				{
				case EShopKind::Grow:       Body = FLinearColor(0.30f, 0.45f, 0.28f); Sign = FLinearColor(0.30f, 0.85f, 0.35f); break; // groen
				case EShopKind::Furniture:  Body = FLinearColor(0.45f, 0.35f, 0.55f); Sign = FLinearColor(0.65f, 0.45f, 0.85f); break; // paars
				case EShopKind::Supplies:   Body = FLinearColor(0.30f, 0.42f, 0.58f); Sign = FLinearColor(0.30f, 0.65f, 0.95f); break; // blauw
				case EShopKind::GasStation: Body = FLinearColor(0.55f, 0.18f, 0.16f); Sign = FLinearColor(0.95f, 0.25f, 0.20f); break; // rood
				default:                    Body = CityFacade(HA); Sign = FLinearColor(0.85f, 0.80f, 0.55f); break; // appartement
				}
				if (Kind == EShopKind::Apartment)
				{
					BuildApartmentBlock(CX, CY, TopZ, ddx, ddy, 4 + (int32)(HA % 3u), Body, Sign);
				}
				else
				{
					BuildEnterableBuilding(FVector(CX, CY, 0.f), TopZ, Foot, Height, ddx, ddy, Kind, Body, Sign);
				}
				continue;
			}

			// Generiek blok: meestal een RIJ huisjes (rijtjeshuizen), soms een hoge flat.
			const uint32 H = CityHash(i, j);
			const float FloorH = 330.f;
			const FLinearColor Glass(0.30f, 0.45f, 0.55f);
			const FLinearColor DoorC(0.12f, 0.10f, 0.09f);

			if ((H % 4u) != 0u) // ~3/4 -> rijtjeshuizen die het hele lot vullen
			{
				BuildRowHouses(CX, CY, TopZ, ddx, ddy, H);
				continue;
			}

			// Hoge flat (1/4 van de blokken): volle voetafdruk, plat dak.
			const FLinearColor Body = CityFacade(H);
			const float Foot = BlockSize - 2.f * SidewalkWidth;
			const float HalfF = Foot * 0.5f;
			const int32 Floors = 4 + (int32)((H >> 2) % 5);
			const float BH = Floors * FloorH;

			AddBox(Cube, FVector(CX, CY, TopZ + BH * 0.5f), FVector(Foot, Foot, BH), Body, true);
			AddBox(Cube, FVector(CX, CY, TopZ + FloorH * 0.5f), FVector(Foot + 8.f, Foot + 8.f, FloorH), Body * 0.5f, false);
			{
				const FVector Nd = (ddx != 0) ? FVector((float)ddx, 0.f, 0.f) : FVector(0.f, (float)ddy, 0.f);
				const FVector DoorPos = FVector(CX, CY, TopZ + 105.f) + Nd * (HalfF + 5.f);
				const FVector DoorSize = (ddx != 0) ? FVector(8.f, 95.f, 210.f) : FVector(95.f, 8.f, 210.f);
				AddBox(Cube, DoorPos, DoorSize, DoorC, false);
			}
			for (int32 f = 1; f < Floors; ++f)
			{
				const float Z = TopZ + f * FloorH + FloorH * 0.5f;
				const float WinW = Foot * 0.8f;
				const float WinH = FloorH * 0.45f;
				AddBox(Cube, FVector(CX, CY + HalfF + 2.f, Z), FVector(WinW, 4.f, WinH), Glass, false);
				AddBox(Cube, FVector(CX, CY - HalfF - 2.f, Z), FVector(WinW, 4.f, WinH), Glass, false);
				AddBox(Cube, FVector(CX + HalfF + 2.f, CY, Z), FVector(4.f, WinW, WinH), Glass, false);
				AddBox(Cube, FVector(CX - HalfF - 2.f, CY, Z), FVector(4.f, WinW, WinH), Glass, false);
			}
			AddBox(Cube, FVector(CX, CY, TopZ + BH + 9.f), FVector(Foot + 14.f, Foot + 14.f, 18.f), Body * 0.6f, false);
		}
	}
}

void ACityGenerator::BuildRowHouses(float CX, float CY, float TopZ, int32 Ddx, int32 Ddy, uint32 Seed)
{
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!Cube) { return; }

	UStaticMesh* Cyl = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));

	const float FloorH = 330.f;
	const float FootMax = BlockSize - 2.f * SidewalkWidth;
	const bool bAlongX = (Ddx != 0);          // straat-normaal langs X -> rij loopt langs Y, en omgekeerd
	const FVector N = bAlongX ? FVector((float)Ddx, 0.f, 0.f) : FVector(0.f, (float)Ddy, 0.f); // naar de straat
	const FVector Tt = bAlongX ? FVector(0.f, 1.f, 0.f) : FVector(1.f, 0.f, 0.f);              // langs de rij

	const float RowLen = FootMax;
	const float Depth = FootMax * 0.40f;        // ondieper -> ruimte voor een voortuin
	const float YardDepth = FootMax - Depth;    // voortuin tussen gevel en stoep
	// Schuif het bouwblok naar ACHTEREN (weg van de straat) zodat de voortuin vooraan ontstaat.
	const FVector BC = FVector(CX, CY, 0.f) + N * (-(FootMax - Depth) * 0.5f);
	const float BCX = BC.X, BCY = BC.Y;
	const int32 Units = 3 + (int32)(Seed % 2u); // 3-4 huisjes
	const float UnitLen = RowLen / Units;
	const float WallH = 2.f * FloorH;           // gelijke goothoogte -> doorlopend dak
	const float HalfD = Depth * 0.5f;

	const FLinearColor Glass(0.30f, 0.45f, 0.55f);
	const FLinearColor DoorC(0.12f, 0.10f, 0.09f);
	const FLinearColor Roof(0.36f, 0.17f, 0.13f);
	const FLinearColor Seam(0.10f, 0.09f, 0.08f);
	const FLinearColor Grass(0.16f, 0.34f, 0.13f);
	const FLinearColor Path(0.60f, 0.58f, 0.52f);
	const FLinearColor Hedge(0.13f, 0.30f, 0.11f);

	// Voortuin-gras over de hele lot-breedte, vóór de gevels.
	const FVector YardC = FVector(CX, CY, TopZ + 3.f) + N * (Depth * 0.5f);
	const FVector YardSize = bAlongX ? FVector(YardDepth, RowLen, 6.f) : FVector(RowLen, YardDepth, 6.f);
	AddBox(Cube, YardC, YardSize, Grass, false);

	for (int32 u = 0; u < Units; ++u)
	{
		const uint32 hu = CityHash((int32)Seed + u * 101 + 9, u * 53 + 17);
		const float Along = -RowLen * 0.5f + (u + 0.5f) * UnitLen;
		const float UX = BCX + Tt.X * Along;
		const float UY = BCY + Tt.Y * Along;
		const FLinearColor Body = CityFacade(hu);

		// Romp van dit huisje (deelt de zijmuren met de buren).
		const FVector BodySize = bAlongX ? FVector(Depth, UnitLen, WallH) : FVector(UnitLen, Depth, WallH);
		AddBox(Cube, FVector(UX, UY, TopZ + WallH * 0.5f), BodySize, Body, true);
		const FVector PlintSize = bAlongX ? FVector(Depth + 6.f, UnitLen, FloorH) : FVector(UnitLen, Depth + 6.f, FloorH);
		AddBox(Cube, FVector(UX, UY, TopZ + FloorH * 0.5f), PlintSize, Body * 0.55f, false);

		// Voordeur aan de straatkant (N).
		const FVector DoorPos = FVector(UX, UY, TopZ + 105.f) + N * (HalfD + 5.f);
		const FVector DoorSize = bAlongX ? FVector(8.f, 95.f, 205.f) : FVector(95.f, 8.f, 205.f);
		AddBox(Cube, DoorPos, DoorSize, DoorC, false);

		// Ramen op de straatgevel.
		for (int32 f = 0; f < 2; ++f)
		{
			const float Z = TopZ + f * FloorH + FloorH * 0.6f;
			const float WinW = UnitLen * 0.42f;
			const float SideOff = UnitLen * 0.24f;
			const FVector WinSize = bAlongX ? FVector(4.f, WinW, FloorH * 0.4f) : FVector(WinW, 4.f, FloorH * 0.4f);
			if (f == 0)
			{
				AddBox(Cube, FVector(UX, UY, Z) + N * (HalfD + 2.f) + Tt * SideOff, WinSize, Glass, false);
			}
			else
			{
				AddBox(Cube, FVector(UX, UY, Z) + N * (HalfD + 2.f) - Tt * SideOff, WinSize, Glass, false);
				AddBox(Cube, FVector(UX, UY, Z) + N * (HalfD + 2.f) + Tt * SideOff, WinSize, Glass, false);
			}
		}

		// Looppad van de deur naar de stoep door de voortuin.
		const float PathLen = YardDepth + SidewalkWidth + 20.f;
		const FVector PathC = FVector(CX, CY, TopZ + 4.f) + N * (Depth * 0.5f + SidewalkWidth * 0.5f) + Tt * Along;
		const FVector PathSize = bAlongX ? FVector(PathLen, 90.f, 8.f) : FVector(90.f, PathLen, 8.f);
		AddBox(Cube, PathC, PathSize, Path, false);

		// Naad-richel tussen de huisjes.
		if (u < Units - 1)
		{
			const float Edge = Along + UnitLen * 0.5f;
			const FVector SeamPos = FVector(BCX + Tt.X * Edge, BCY + Tt.Y * Edge, TopZ + WallH * 0.5f);
			const FVector SeamSize = bAlongX ? FVector(Depth + 10.f, 6.f, WallH) : FVector(6.f, Depth + 10.f, WallH);
			AddBox(Cube, SeamPos, SeamSize, Seam, false);
		}
	}

	// Lage heg langs de voortuin-rand, met een opening bij elk looppad.
	{
		const float HedgeH = 60.f;
		const float FrontN = FootMax * 0.5f - 12.f; // net binnen de stoep
		const float GapHalf = 60.f;
		float Cursor = -RowLen * 0.5f;
		auto AddHedge = [&](float A0, float A1)
		{
			if (A1 - A0 < 15.f) { return; }
			const float Mid = (A0 + A1) * 0.5f;
			const float Len = A1 - A0;
			const FVector P = FVector(CX, CY, TopZ + HedgeH * 0.5f) + N * FrontN + Tt * Mid;
			const FVector S = bAlongX ? FVector(22.f, Len, HedgeH) : FVector(Len, 22.f, HedgeH);
			AddBox(Cube, P, S, Hedge, false);
		};
		for (int32 u = 0; u < Units; ++u)
		{
			const float G = -RowLen * 0.5f + (u + 0.5f) * UnitLen;
			AddHedge(Cursor, G - GapHalf);
			Cursor = G + GapHalf;
		}
		AddHedge(Cursor, RowLen * 0.5f);
	}

	// Een boompje in een hoek van de voortuin.
	if (Cyl && Sphere)
	{
		const FVector TreeBase = FVector(CX, CY, TopZ) + N * (Depth * 0.5f + YardDepth * 0.45f) + Tt * (RowLen * 0.5f - 130.f);
		AddBox(Cyl, TreeBase + FVector(0.f, 0.f, 95.f), FVector(26.f, 26.f, 190.f), FLinearColor(0.28f, 0.18f, 0.10f), false);
		AddBox(Sphere, TreeBase + FVector(0.f, 0.f, 235.f), FVector(180.f, 180.f, 170.f), FLinearColor(0.16f, 0.36f, 0.14f), false);
		AddBox(Sphere, TreeBase + FVector(40.f, 20.f, 300.f), FVector(130.f, 130.f, 120.f), FLinearColor(0.19f, 0.40f, 0.16f), false);
	}

	// Eén doorlopend zadeldak over de hele rij.
	const bool RidgeAlongX = !bAlongX;
	const float RoofW = bAlongX ? (Depth + 30.f) : (RowLen + 20.f);
	const float RoofD = bAlongX ? (RowLen + 20.f) : (Depth + 30.f);
	const float RoofRidgeH = Depth * 0.5f;
	AddGableRoof(FVector(BCX, BCY, TopZ + WallH), RoofW, RoofD, RoofRidgeH, RidgeAlongX, Roof);

	// Sluit de twee dak-uiteinden (gable-driehoeken) met verticale plakjes, zodat het dak niet
	// openstaat aan de zijkanten van de rij.
	{
		const int32 NSl = 20;
		const float SliceW = Depth / NSl;
		const float HalfDepth = Depth * 0.5f;
		const FLinearColor GableC = CityFacade(Seed + 3);
		for (int32 endi = 0; endi < 2; ++endi)
		{
			const float EndAlong = (endi == 0) ? (-RowLen * 0.5f) : (RowLen * 0.5f);
			for (int32 k = 0; k < NSl; ++k)
			{
				const float n = -HalfDepth + (k + 0.5f) * SliceW;
				// Hoogte gemeten aan de NOK-kant van het plakje -> reikt tot de daklijn (overlapt iets,
				// geen zwarte happen). Veel fijne plakjes -> gladde driehoek.
				const float InnerAbs = FMath::Max(0.f, FMath::Abs(n) - SliceW * 0.5f);
				const float hh = RoofRidgeH * (1.f - InnerAbs / HalfDepth);
				if (hh < 5.f) { continue; }
				const FVector P = FVector(BCX, BCY, TopZ + WallH + hh * 0.5f) + Tt * EndAlong + N * n;
				const FVector S = bAlongX ? FVector(SliceW + 2.f, 16.f, hh) : FVector(16.f, SliceW + 2.f, hh);
				AddBox(Cube, P, S, GableC, false);
			}
		}
	}
}

void ACityGenerator::BuildApartmentBlock(float CX, float CY, float TopZ, int32 Ddx, int32 Ddy, int32 Floors, const FLinearColor& Body, const FLinearColor& Sign)
{
	UWorld* W = GetWorld();
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!W || !Cube) { return; }

	const int32 NF = FMath::Clamp(Floors, 3, 7);
	const float FloorH = 330.f;
	const float Foot = BlockSize - 2.f * SidewalkWidth;
	const float Half = Foot * 0.5f;
	const float T = 20.f;
	const float TotalH = NF * FloorH;
	const float DoorW = 240.f;
	const float DoorH = 250.f;
	const FLinearColor FloorC(0.30f, 0.29f, 0.27f);
	const FLinearColor Glass(0.30f, 0.45f, 0.55f);

	const int32 DoorSide = (Ddx > 0) ? 0 : (Ddx < 0) ? 1 : (Ddy > 0) ? 2 : 3;
	const FVector N((float)Ddx, (float)Ddy, 0.f);

	// Buitenmuren (volle hoogte); de straatgevel heeft op de begane grond een deur-opening.
	const float WallZc = TopZ + TotalH * 0.5f;
	auto BuildWall = [&](int32 side)
	{
		const bool bDoor = (side == DoorSide);
		const bool bAlongY = (side == 0 || side == 1);
		const float WallLen = Foot + 2.f * T;
		FVector Pos(CX, CY, WallZc);
		if (side == 0) { Pos.X = CX + Half + T * 0.5f; }
		else if (side == 1) { Pos.X = CX - Half - T * 0.5f; }
		else if (side == 2) { Pos.Y = CY + Half + T * 0.5f; }
		else { Pos.Y = CY - Half - T * 0.5f; }
		if (!bDoor)
		{
			AddBox(Cube, Pos, bAlongY ? FVector(T, WallLen, TotalH) : FVector(WallLen, T, TotalH), Body, true);
			return;
		}
		const float SegLen = (WallLen - DoorW) * 0.5f;
		const float Off = (DoorW + SegLen) * 0.5f;
		if (bAlongY)
		{
			AddBox(Cube, Pos + FVector(0, Off, 0), FVector(T, SegLen, TotalH), Body, true);
			AddBox(Cube, Pos + FVector(0, -Off, 0), FVector(T, SegLen, TotalH), Body, true);
			AddBox(Cube, FVector(Pos.X, CY, TopZ + (DoorH + TotalH) * 0.5f), FVector(T, DoorW, TotalH - DoorH), Body, true);
		}
		else
		{
			AddBox(Cube, Pos + FVector(Off, 0, 0), FVector(SegLen, T, TotalH), Body, true);
			AddBox(Cube, Pos + FVector(-Off, 0, 0), FVector(SegLen, T, TotalH), Body, true);
			AddBox(Cube, FVector(CX, Pos.Y, TopZ + (DoorH + TotalH) * 0.5f), FVector(DoorW, T, TotalH - DoorH), Body, true);
		}
	};
	for (int32 s = 0; s < 4; ++s) { BuildWall(s); }

	// Ramen per verdieping op de 4 gevels. Op de begane grond NIET op de deurwand (anders een
	// "blauw blok" in de deuropening).
	for (int32 f = 0; f < NF; ++f)
	{
		const float z = TopZ + f * FloorH + FloorH * 0.55f;
		const float WinW = Foot * 0.7f, WinH = FloorH * 0.4f;
		const bool bGround = (f == 0);
		if (!(bGround && DoorSide == 2)) { AddBox(Cube, FVector(CX, CY + Half + 2.f, z), FVector(WinW, 4.f, WinH), Glass, false); }
		if (!(bGround && DoorSide == 3)) { AddBox(Cube, FVector(CX, CY - Half - 2.f, z), FVector(WinW, 4.f, WinH), Glass, false); }
		if (!(bGround && DoorSide == 0)) { AddBox(Cube, FVector(CX + Half + 2.f, CY, z), FVector(4.f, WinW, WinH), Glass, false); }
		if (!(bGround && DoorSide == 1)) { AddBox(Cube, FVector(CX - Half - 2.f, CY, z), FVector(4.f, WinW, WinH), Glass, false); }
	}

	// Kern (trappenhuis + liftschacht) in de achter-linker hoek. Groot gat: de trap loopt over de
	// volle lengte (lange, ruime steken) met de lift ernaast.
	const float LaneW = 150.f;              // breedte van één trap-baan
	const float StairXW = 2.f * LaneW;      // twee banen naast elkaar (switchback)
	const float Gap = 70.f, ElevW = 240.f, ElevD = 300.f;
	const float StairRunY = 640.f;          // looplengte van een halve steek (langs Y) -> ruime trap
	const float CoreW = StairXW + Gap + ElevW;
	const float CoreD = FMath::Max(StairRunY, ElevD);
	const float HoleMinX = CX - Half + 25.f, HoleMinY = CY - Half + 25.f;
	const float HoleMaxX = HoleMinX + CoreW, HoleMaxY = HoleMinY + CoreD;

	// Verdiepingsvloeren f=1..NF-1 met een L-gat over de kern (trap loopt erdoor); + binnenlicht.
	for (int32 f = 1; f < NF; ++f)
	{
		const float z = TopZ + f * FloorH;
		const float frontYs = (CY + Half) - HoleMaxY;
		if (frontYs > 1.f) { AddBox(Cube, FVector(CX, (HoleMaxY + CY + Half) * 0.5f, z), FVector(Foot, frontYs, 12.f), FloorC, true); }
		const float sideXs = (CX + Half) - HoleMaxX;
		if (sideXs > 1.f) { AddBox(Cube, FVector((HoleMaxX + CX + Half) * 0.5f, (CY - Half + HoleMaxY) * 0.5f, z), FVector(sideXs, HoleMaxY - (CY - Half), 12.f), FloorC, true); }
		AddInteriorLight(FVector(CX, CY, z - 55.f));
	}
	// Dichte dakvloer bovenop (plafond van de bovenste verdieping, GEEN gat -> de trap stopt op de top-etage).
	AddBox(Cube, FVector(CX, CY, TopZ + NF * FloorH), FVector(Foot, Foot, 14.f), FloorC, true);
	AddInteriorLight(FVector(CX, CY, TopZ + NF * FloorH - 55.f));

	// Echt trappenhuis: switchback met SOLIDE treden + bordes. Begin en eind liggen allebei aan de
	// voorkant (yFront), zodat je elke verdieping netjes op de vloer stapt (geen losse zwevende treden).
	{
		const int32 SPF = 12;                        // treden per halve steek (ruime, gentle trap)
		const float HalfRise = FloorH * 0.5f;
		const float Rise = HalfRise / SPF;           // ~20cm per trede
		const float RunY = StairRunY / SPF;          // diepte per trede
		const float yFront = HoleMinY + StairRunY;   // start/eind (richting de vloer)
		const float yBack = HoleMinY;                // bordes-kant
		const float xA = HoleMinX + LaneW * 0.5f;    // baan omhoog naar bordes
		const float xB = HoleMinX + LaneW * 1.5f;    // baan van bordes naar volgende verdieping
		const FLinearColor StepC = Body * 0.7f;
		const FLinearColor LandC = Body * 0.5f;
		for (int32 f = 0; f < NF - 1; ++f) // alleen tussen de woon-etages, niet naar het dak
		{
			const float zf = TopZ + f * FloorH;
			// Steek 1 (baan A): van de voorkant naar achter, omhoog naar halve hoogte. Solide vanaf de vloer.
			for (int32 s = 0; s < SPF; ++s)
			{
				const float TreadTop = zf + (s + 1) * Rise;
				const float y = yFront - (s + 0.5f) * RunY;
				AddBox(Cube, FVector(xA, y, TreadTop - 6.f), FVector(LaneW, RunY + 5.f, 12.f), StepC, true); // dunne trede
			}
			// Bordes achterin op halve hoogte (verbindt baan A en B).
			AddBox(Cube, FVector(HoleMinX + StairXW * 0.5f, yBack + RunY * 0.6f, zf + HalfRise - 8.f), FVector(StairXW, RunY * 1.4f, 16.f), LandC, true);
			// Steek 2 (baan B): van achter naar de voorkant, omhoog naar de volgende verdieping. Solide vanaf het bordes.
			for (int32 s = 0; s < SPF; ++s)
			{
				const float TreadTop = zf + HalfRise + (s + 1) * Rise;
				const float y = yBack + (s + 0.5f) * RunY;
				AddBox(Cube, FVector(xB, y, TreadTop - 6.f), FVector(LaneW, RunY + 5.f, 12.f), StepC, true); // dunne trede
			}
		}
	}

	// Werkende lift in de kern, naast de trap.
	{
		const float ElevCX = HoleMinX + StairXW + Gap + ElevW * 0.5f;
		const float ElevCY = HoleMinY + ElevD * 0.5f;
		FActorSpawnParameters SP; SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		if (ACityElevator* Ele = W->SpawnActor<ACityElevator>(ACityElevator::StaticClass(), FTransform(FVector(ElevCX, ElevCY, TopZ + 4.f)), SP))
		{
			Ele->Setup(TopZ + 4.f, FloorH, NF, ElevW - 20.f, ElevD - 20.f, Body * 0.5f);
		}
	}

	// Plat dak + parapet.
	AddBox(Cube, FVector(CX, CY, TopZ + TotalH + 10.f), FVector(Foot + 16.f, Foot + 16.f, 20.f), Body * 0.6f, false);

	// Donker naambord + gloeiende neon-tekst + neonlicht.
	{
		const float SignZ = TopZ + DoorH + 55.f;
		const float SignW = Foot * 0.9f;
		const FVector SignPos = FVector(CX, CY, SignZ) + N * (Half + T);
		const FVector SignSize = (DoorSide <= 1) ? FVector(10.f, SignW, 110.f) : FVector(SignW, 10.f, 110.f);
		AddBox(Cube, SignPos, SignSize, FLinearColor(0.02f, 0.02f, 0.03f), false);
		const FLinearColor Neon = (Sign * 1.8f).GetClamped(0.f, 1.f);
		AddSignText(FVector(CX, CY, SignZ) + N * (Half + T + 10.f), Ddx, Ddy, TEXT("APARTMENTS"), Neon, 65.f);

		UPointLightComponent* NeonPL = NewObject<UPointLightComponent>(this);
		NeonPL->SetupAttachment(Root); NeonPL->RegisterComponent();
		NeonPL->SetWorldLocation(FVector(CX, CY, SignZ - 20.f) + N * (Half + T + 170.f));
		NeonPL->SetMobility(EComponentMobility::Movable);
		NeonPL->SetAttenuationRadius(800.f); NeonPL->SetIntensity(3200.f);
		NeonPL->SetLightColor(Sign); NeonPL->SetCastShadows(false);
	}

	// Werkende voordeur.
	{
		const FVector Tang(-N.Y, N.X, 0.f);
		const FVector OpeningCenter = FVector(CX, CY, TopZ) + N * (Half - 2.f);
		const FVector HingePos = OpeningCenter - Tang * (DoorW * 0.5f);
		const float DoorYaw = FMath::RadiansToDegrees(FMath::Atan2(Tang.Y, Tang.X));
		FActorSpawnParameters DSP; DSP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		if (ACityDoor* Door = W->SpawnActor<ACityDoor>(ACityDoor::StaticClass(), FTransform(FRotator(0.f, DoorYaw, 0.f), HingePos), DSP))
		{
			Door->Setup(DoorW - 6.f, DoorH - 6.f, FLinearColor(0.10f, 0.08f, 0.07f));
		}
	}
}

void ACityGenerator::AddInteriorLight(const FVector& WorldLoc)
{
	UPointLightComponent* PL = NewObject<UPointLightComponent>(this);
	PL->SetupAttachment(Root);
	PL->RegisterComponent();
	PL->SetWorldLocation(WorldLoc);
	PL->SetMobility(EComponentMobility::Movable);
	PL->SetAttenuationRadius(900.f);
	PL->SetIntensity(7000.f);
	PL->SetLightColor(FLinearColor(1.f, 0.92f, 0.78f));
	PL->SetCastShadows(false);
}

void ACityGenerator::BuildEnterableBuilding(const FVector& CenterXY, float BaseZ, float Foot, float Height,
	int32 DoorDirX, int32 DoorDirY, EShopKind Kind, const FLinearColor& Body, const FLinearColor& Sign)
{
	UWorld* W = GetWorld();
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!W || !Cube) { return; }

	const float CX = CenterXY.X, CY = CenterXY.Y;
	const float T = 20.f;                 // wanddikte
	const float Half = Foot * 0.5f;
	const float DoorW = 240.f;            // deur-opening breedte
	const float DoorH = FMath::Min(280.f, Height - 40.f); // deur-opening hoogte
	const FLinearColor Floor(0.30f, 0.29f, 0.27f);
	const FLinearColor Ceil(0.22f, 0.22f, 0.24f);

	// Vloer + plafond.
	AddBox(Cube, FVector(CX, CY, BaseZ + 5.f), FVector(Foot + 2.f * T, Foot + 2.f * T, 10.f), Floor, true);
	AddBox(Cube, FVector(CX, CY, BaseZ + Height), FVector(Foot + 2.f * T, Foot + 2.f * T, T), Ceil, true);

	// Welke wand krijgt de deur? 0=+X,1=-X,2=+Y,3=-Y.
	int32 DoorSide = 0;
	if (DoorDirX > 0) { DoorSide = 0; } else if (DoorDirX < 0) { DoorSide = 1; }
	else if (DoorDirY > 0) { DoorSide = 2; } else { DoorSide = 3; }

	const float WallZ = BaseZ + Height * 0.5f;
	// Wand-bouwer: volle wand, of (met deur) twee segmenten + latei erboven.
	auto BuildWall = [&](int32 side)
	{
		const bool bDoor = (side == DoorSide);
		// +X / -X wanden lopen langs Y; +Y / -Y wanden lopen langs X.
		const bool bAlongY = (side == 0 || side == 1);
		const float WallLen = Foot + 2.f * T;
		FVector Pos = FVector(CX, CY, WallZ);
		if (side == 0) { Pos.X = CX + Half + T * 0.5f; }
		else if (side == 1) { Pos.X = CX - Half - T * 0.5f; }
		else if (side == 2) { Pos.Y = CY + Half + T * 0.5f; }
		else { Pos.Y = CY - Half - T * 0.5f; }

		if (!bDoor)
		{
			const FVector Size = bAlongY ? FVector(T, WallLen, Height) : FVector(WallLen, T, Height);
			AddBox(Cube, Pos, Size, Body, true);
			return;
		}
		// Deur-wand: 2 zijsegmenten + latei boven de opening.
		const float SegLen = (WallLen - DoorW) * 0.5f;
		const float Off = (DoorW + SegLen) * 0.5f;
		if (bAlongY)
		{
			AddBox(Cube, Pos + FVector(0, Off, 0),  FVector(T, SegLen, Height), Body, true);
			AddBox(Cube, Pos + FVector(0, -Off, 0), FVector(T, SegLen, Height), Body, true);
			AddBox(Cube, FVector(Pos.X, CY, BaseZ + (DoorH + Height) * 0.5f), FVector(T, DoorW, Height - DoorH), Body, true);
		}
		else
		{
			AddBox(Cube, Pos + FVector(Off, 0, 0),  FVector(SegLen, T, Height), Body, true);
			AddBox(Cube, Pos + FVector(-Off, 0, 0), FVector(SegLen, T, Height), Body, true);
			AddBox(Cube, FVector(CX, Pos.Y, BaseZ + (DoorH + Height) * 0.5f), FVector(DoorW, T, Height - DoorH), Body, true);
		}
	};
	for (int32 s = 0; s < 4; ++s) { BuildWall(s); }

	// Naambord boven de deur (buitenkant), in de accentkleur, met leesbare 3D-tekst.
	{
		const FVector N(DoorDirX, DoorDirY, 0.f);
		const float SignZ = BaseZ + DoorH + 55.f;
		const float SignW = Foot * 0.9f; // bord over bijna de hele gevel
		const FVector SignPos = FVector(CX, CY, SignZ) + N * (Half + T);
		const FVector SignSize = (DoorSide <= 1) ? FVector(10.f, SignW, 110.f) : FVector(SignW, 10.f, 110.f);
		// DONKER achterbord -> de felle neon-letters lichten erop op en zijn leesbaar.
		AddBox(Cube, SignPos, SignSize, FLinearColor(0.02f, 0.02f, 0.03f), false);

		FString Name;
		switch (Kind)
		{
		case EShopKind::Grow:       Name = TEXT("GROW SHOP"); break;
		case EShopKind::Furniture:  Name = TEXT("FURNITURE STORE"); break;
		case EShopKind::Supplies:   Name = TEXT("SUPPLIES"); break;
		case EShopKind::GasStation: Name = TEXT("GAS STATION"); break;
		default:                    Name = TEXT("APARTMENTS"); break;
		}
		// Neon: felle, oplichtende letters (TextRender is unlit -> gloeit) in de accentkleur.
		const FLinearColor Neon = (Sign * 1.8f).GetClamped(0.f, 1.f);
		const FVector TextPos = FVector(CX, CY, SignZ) + N * (Half + T + 10.f);
		AddSignText(TextPos, DoorDirX, DoorDirY, Name, Neon, 65.f);

		// Zacht gekleurd neonlicht dat NAAR BUITEN schijnt (uit de muur vandaan -> geen felle hotspot).
		UPointLightComponent* NeonPL = NewObject<UPointLightComponent>(this);
		NeonPL->SetupAttachment(Root);
		NeonPL->RegisterComponent();
		NeonPL->SetWorldLocation(FVector(CX, CY, SignZ - 20.f) + N * (Half + T + 170.f));
		NeonPL->SetMobility(EComponentMobility::Movable);
		NeonPL->SetAttenuationRadius(800.f);
		NeonPL->SetIntensity(3200.f);
		NeonPL->SetLightColor(Sign);
		NeonPL->SetCastShadows(false);
	}

	// Werkende deur in de opening: scharniert open als je dichtbij komt.
	if (W)
	{
		const FVector N(DoorDirX, DoorDirY, 0.f);
		const FVector Tang(-N.Y, N.X, 0.f);
		const FVector OpeningCenter = FVector(CX, CY, BaseZ) + N * (Half - 2.f);
		const FVector HingePos = OpeningCenter - Tang * (DoorW * 0.5f);
		const float DoorYaw = FMath::RadiansToDegrees(FMath::Atan2(Tang.Y, Tang.X));
		FActorSpawnParameters DSP; DSP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		if (ACityDoor* Door = W->SpawnActor<ACityDoor>(ACityDoor::StaticClass(), FTransform(FRotator(0.f, DoorYaw, 0.f), HingePos), DSP))
		{
			Door->Setup(DoorW - 6.f, DoorH - 6.f, FLinearColor(0.10f, 0.08f, 0.07f));
		}
	}

	// Binnenlicht.
	AddInteriorLight(FVector(CX, CY, BaseZ + Height - 60.f));

	// Winkels: balie achterin (tegenover de deur), gericht naar de deur. Appartement: geen balie.
	if (Kind != EShopKind::Apartment)
	{
		const FVector N(DoorDirX, DoorDirY, 0.f); // wijst naar de deur (uitgang)
		const FVector CounterPos = FVector(CX, CY, BaseZ + 10.f) - N * (Half * 0.55f);
		const float Yaw = FMath::RadiansToDegrees(FMath::Atan2(N.Y, N.X)); // balie kijkt naar de deur
		FActorSpawnParameters SP; SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		if (AStoreCounter* Counter = W->SpawnActor<AStoreCounter>(AStoreCounter::StaticClass(), FTransform(FRotator(0.f, Yaw, 0.f), CounterPos), SP))
		{
			Counter->Kind = Kind;
			Counter->SetupVisual(Sign);
		}
	}

	// Gas station: luifel op pilaren + 2 pompen vóór de deur op de stoep.
	if (Kind == EShopKind::GasStation)
	{
		const FVector N(DoorDirX, DoorDirY, 0.f);
		const FVector Fwd = N * (Half + 380.f); // vóór het gebouw
		const FLinearColor Metal(0.85f, 0.85f, 0.88f), Dark(0.15f, 0.15f, 0.17f);
		// Luifel.
		AddBox(Cube, FVector(CX + Fwd.X, CY + Fwd.Y, BaseZ + 330.f), FVector(620.f, 620.f, 30.f), Metal, true);
		// Lichtbak op de voorrand van de luifel met een groot, goed leesbaar GAS-bord (niet geblokkeerd).
		{
			const FVector FasciaC = FVector(CX + Fwd.X, CY + Fwd.Y, BaseZ + 345.f) + N * 305.f;
			const FVector FasciaSize = (DoorDirX != 0) ? FVector(12.f, 600.f, 90.f) : FVector(600.f, 12.f, 90.f);
			AddBox(Cube, FasciaC, FasciaSize, FLinearColor(0.02f, 0.02f, 0.03f), false);
			AddSignText(FVector(CX + Fwd.X, CY + Fwd.Y, BaseZ + 345.f) + N * 313.f, DoorDirX, DoorDirY, TEXT("GAS"), FLinearColor(1.f, 0.85f, 0.2f), 95.f);
			UPointLightComponent* GasPL = NewObject<UPointLightComponent>(this);
			GasPL->SetupAttachment(Root); GasPL->RegisterComponent();
			GasPL->SetWorldLocation(FVector(CX + Fwd.X, CY + Fwd.Y, BaseZ + 320.f) + N * 360.f);
			GasPL->SetMobility(EComponentMobility::Movable);
			GasPL->SetAttenuationRadius(700.f); GasPL->SetIntensity(3500.f);
			GasPL->SetLightColor(FLinearColor(1.f, 0.8f, 0.3f)); GasPL->SetCastShadows(false);
		}
		// 4 pilaren.
		for (int32 px = -1; px <= 1; px += 2)
		{
			for (int32 py = -1; py <= 1; py += 2)
			{
				AddBox(Cube, FVector(CX + Fwd.X + px * 260.f, CY + Fwd.Y + py * 260.f, BaseZ + 165.f), FVector(24.f, 24.f, 330.f), Dark, true);
			}
		}
		// 2 pompen.
		AddBox(Cube, FVector(CX + Fwd.X - 120.f, CY + Fwd.Y, BaseZ + 70.f), FVector(50.f, 90.f, 140.f), FLinearColor(0.90f, 0.30f, 0.20f), true);
		AddBox(Cube, FVector(CX + Fwd.X + 120.f, CY + Fwd.Y, BaseZ + 70.f), FVector(50.f, 90.f, 140.f), FLinearColor(0.90f, 0.30f, 0.20f), true);
	}
}
