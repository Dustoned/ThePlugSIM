#include "World/CityGenerator.h"

#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/TextRenderComponent.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/PlayerStart.h"
#include "World/StoreCounter.h"
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

			const float TopZ = GroundZ + CurbHeight; // gebouw/winkel staat op de stoep

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
				default:                    Body = Facades[HA % UE_ARRAY_COUNT(Facades)]; Sign = FLinearColor(0.85f, 0.80f, 0.55f); break; // appartement
				}
				BuildEnterableBuilding(FVector(CX, CY, 0.f), TopZ, Foot, Height, ddx, ddy, Kind, Body, Sign);
				continue;
			}

			// Generiek gebouw: deterministische mix van LAGE huizen (schuin dak, deur) en HOGE flats.
			const uint32 H = CityHash(i, j);
			const float FloorH = 330.f;
			const FLinearColor Body = Facades[H % UE_ARRAY_COUNT(Facades)];
			const FLinearColor Glass(0.30f, 0.45f, 0.55f);
			const FLinearColor DoorC(0.12f, 0.10f, 0.09f);
			const bool bHouse = (H % 4u) != 0u; // ~3/4 lage huizen, 1/4 hoge flats

			const float FootMax = BlockSize - 2.f * SidewalkWidth;
			const int32 Floors = bHouse ? (1 + (int32)((H >> 2) % 2)) : (4 + (int32)((H >> 2) % 5)); // huis 1-2, flat 4-8
			const float BH = Floors * FloorH;
			const float Foot = bHouse ? FootMax * (0.40f + 0.06f * ((H >> 5) % 3)) : FootMax; // huis kleiner -> open lot
			const float HalfF = Foot * 0.5f;

			// Romp + plint (begane grond).
			AddBox(Cube, FVector(CX, CY, TopZ + BH * 0.5f), FVector(Foot, Foot, BH), Body, true);
			AddBox(Cube, FVector(CX, CY, TopZ + FloorH * 0.5f), FVector(Foot + 8.f, Foot + 8.f, FloorH), Body * 0.5f, false);

			// Voordeur aan de straatkant (richting het midden).
			{
				const FVector Nd = (ddx != 0) ? FVector((float)ddx, 0.f, 0.f) : FVector(0.f, (float)ddy, 0.f);
				const FVector DoorPos = FVector(CX, CY, TopZ + 105.f) + Nd * (HalfF + 5.f);
				const FVector DoorSize = (ddx != 0) ? FVector(8.f, 95.f, 210.f) : FVector(95.f, 8.f, 210.f);
				AddBox(Cube, DoorPos, DoorSize, DoorC, false);
			}

			// Ramen per verdieping.
			for (int32 f = (bHouse ? 0 : 1); f < Floors; ++f)
			{
				const float Z = TopZ + f * FloorH + FloorH * 0.5f;
				const float WinW = Foot * (bHouse ? 0.6f : 0.8f);
				const float WinH = FloorH * 0.45f;
				AddBox(Cube, FVector(CX, CY + HalfF + 2.f, Z), FVector(WinW, 4.f, WinH), Glass, false);
				AddBox(Cube, FVector(CX, CY - HalfF - 2.f, Z), FVector(WinW, 4.f, WinH), Glass, false);
				AddBox(Cube, FVector(CX + HalfF + 2.f, CY, Z), FVector(4.f, WinW, WinH), Glass, false);
				AddBox(Cube, FVector(CX - HalfF - 2.f, CY, Z), FVector(4.f, WinW, WinH), Glass, false);
			}

			if (bHouse)
			{
				// Groot, dominant zadeldak (donkerrood pannendak) -> echte huis/cottage-vorm. Plus schoorsteen.
				const FLinearColor Roof(0.35f, 0.18f, 0.14f);
				const bool RidgeX = ((H >> 6) & 1u) != 0u;
				const float RidgeH = FMath::Max(BH * 0.85f, 300.f) + (float)(H % 60u);
				AddGableRoof(FVector(CX, CY, TopZ + BH), Foot + 24.f, Foot + 24.f, RidgeH, RidgeX, Roof);
				AddBox(Cube, FVector(CX + Foot * 0.28f, CY + Foot * 0.22f, TopZ + BH + RidgeH * 0.45f), FVector(26.f, 26.f, RidgeH * 0.6f), Body * 0.5f, false);
			}
			else
			{
				// Platte dakrand (parapet).
				AddBox(Cube, FVector(CX, CY, TopZ + BH + 9.f), FVector(Foot + 14.f, Foot + 14.f, 18.f), Body * 0.6f, false);
			}
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
		AddBox(Cube, SignPos, SignSize, Sign, false);

		FString Name;
		switch (Kind)
		{
		case EShopKind::Grow:       Name = TEXT("GROW SHOP"); break;
		case EShopKind::Furniture:  Name = TEXT("FURNITURE STORE"); break;
		case EShopKind::Supplies:   Name = TEXT("SUPPLIES"); break;
		case EShopKind::GasStation: Name = TEXT("GAS STATION"); break;
		default:                    Name = TEXT("APARTMENTS"); break;
		}
		const FVector TextPos = FVector(CX, CY, SignZ) + N * (Half + T + 10.f);
		AddSignText(TextPos, DoorDirX, DoorDirY, Name, FLinearColor::White, 65.f);
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
