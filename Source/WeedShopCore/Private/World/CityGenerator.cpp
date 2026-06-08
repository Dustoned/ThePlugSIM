#include "World/CityGenerator.h"

#include "WeedShopCore.h"
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
#include "World/Atm.h"
#include "World/CityDoor.h"
#include "World/CityElevator.h"
#include "World/RoadNavZone.h"
#include "World/DayNightController.h"
#include "Game/WeedShopGameState.h"
#include "World/DayCycleComponent.h"
#include "NavigationInvokerComponent.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "Components/BrushComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/Scene.h"
#include "Camera/CameraTypes.h"
#include "TimerManager.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"

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
	PrimaryActorTick.bTickEvenWhenPaused = true;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Root->SetMobility(EComponentMobility::Static);
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
	// Warme SPOTLIGHT recht naar beneden, BREDE kegel met ZACHTE randen (grote falloff tussen binnen/
	// buiten-hoek) -> egale lichtpoel i.p.v. een harde zaklampstraal.
	USpotLightComponent* SL = NewObject<USpotLightComponent>(this);
	SL->SetupAttachment(Root);
	SL->RegisterComponent();
	SL->SetWorldLocation(BaseWorld + FVector(0.f, 0.f, PoleH - 6.f));
	SL->SetWorldRotation(FRotator(-90.f, 0.f, 0.f)); // recht omlaag
	SL->SetMobility(EComponentMobility::Movable);
	SL->SetAttenuationRadius(1600.f);
	SL->SetInnerConeAngle(10.f);   // klein -> falloff begint dicht bij het midden
	SL->SetOuterConeAngle(72.f);   // breed -> zachte, uitwaaierende rand
	SL->SetLightColor(FLinearColor(1.f, 0.82f, 0.5f));
	SL->SetIntensity(0.f); // tick zet 'm 's avonds aan
	SL->SetCastShadows(false);
	LampLights.Add(SL);

	// Zachte warme gloed rond de lampkop (omni, laag) -> ambiance, niet alleen de poel op de grond.
	UPointLightComponent* Glow = NewObject<UPointLightComponent>(this);
	Glow->SetupAttachment(Root);
	Glow->RegisterComponent();
	Glow->SetWorldLocation(BaseWorld + FVector(0.f, 0.f, PoleH));
	Glow->SetMobility(EComponentMobility::Movable);
	Glow->SetAttenuationRadius(750.f);
	Glow->SetLightColor(FLinearColor(1.f, 0.84f, 0.55f));
	Glow->SetIntensity(0.f);
	Glow->SetCastShadows(false);
	LampGlows.Add(Glow);
}

void ACityGenerator::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bNavCoverageRetryQueued)
	{
		const UWorld* W = GetWorld();
		if (!W || NavCoverageAttempts >= 4)
		{
			bNavCoverageRetryQueued = false;
		}
		else if (W->GetRealTimeSeconds() >= NextNavCoverageRetryRealTime)
		{
			bNavCoverageRetryQueued = false;
			VerifyCityNavigationCoverage();
		}
	}

	if (LampLights.Num() == 0) { return; }
	LampTickAccum += DeltaSeconds;
	if (LampTickAccum < 0.2f) { return; } // niet elke frame (maar wel vlot genoeg voor de live slider)
	LampTickAccum = 0.f;

	const AWeedShopGameState* GS = GetWorld() ? GetWorld()->GetGameState<AWeedShopGameState>() : nullptr;
	const UDayCycleComponent* DC = GS ? GS->GetDayCycle() : nullptr;
	if (!DC) { return; }
	const float Hour = DC->GetClockHour();
	const int32 WantOn = (Hour < 8.f || Hour >= 17.f) ? 1 : 0;

	// Intensiteit komt van de phone-slider (DayNightController.LampIntensity) -> live regelbaar.
	float Intensity = 42000.f;
	if (ADayNightController* DN = ADayNightController::GetLocal(GetWorld())) { Intensity = DN->LampIntensity; }
	const float SpotI = (WantOn == 1) ? Intensity : 0.f;
	const float GlowI = (WantOn == 1) ? Intensity * 0.062f : 0.f; // gloed schaalt mee met de spot

	const bool bStateChanged = (WantOn != bLampsOn);
	const bool bIntChanged = (WantOn == 1 && !FMath::IsNearlyEqual(SpotI, LastLampApplied, 1.f));
	if (!bStateChanged && !bIntChanged) { return; }
	bLampsOn = WantOn;
	LastLampApplied = SpotI;
	for (ULightComponent* PL : LampLights) { if (PL) { PL->SetIntensity(SpotI); } }
	for (ULightComponent* PL : LampGlows)  { if (PL) { PL->SetIntensity(GlowI); } }
	if (bStateChanged)
	{
		for (UMaterialInstanceDynamic* M : LampHeadMats)
		{
			if (M) { M->SetVectorParameterValue(TEXT("Color"), WantOn ? FLinearColor(1.f, 0.86f, 0.5f) : FLinearColor(0.2f, 0.2f, 0.22f)); }
		}
	}
}

UStaticMeshComponent* ACityGenerator::AddBox(UStaticMesh* MeshAsset, const FVector& CenterWorld,
	const FVector& SizeCm, const FLinearColor& Color, bool bCollides, const FRotator& Rot)
{
	if (!MeshAsset) { return nullptr; }
	UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
	C->SetupAttachment(Root);
	C->SetStaticMesh(MeshAsset);
	C->SetMobility(EComponentMobility::Static);
	C->SetWorldLocation(CenterWorld);
	C->SetWorldRotation(Rot);
	C->SetWorldScale3D(SizeCm / 100.f); // basis-kubus = 100cm
	C->SetCollisionEnabled(bCollides ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
	C->SetCanEverAffectNavigation(bCollides);
	C->RegisterComponent();
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

void ACityGenerator::AddSignText(const FVector& WorldLoc, int32 DirX, int32 DirY, const FString& Text, const FLinearColor& Color, float Size, bool bGlow)
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
	if (bGlow)
	{
		// Unlit/emissive variant van het standaard tekstmateriaal -> letters lichten zelf op (leesbaar 's nachts).
		static UMaterialInterface* GlowMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/_Project/Materials/M_NumGlow.M_NumGlow"));
		if (GlowMat) { T->SetTextMaterial(GlowMat); }
	}
}

void ACityGenerator::AddDoorNumber(const FVector& PlateCenter, int32 DirX, int32 DirY, const FString& Text, float Size)
{
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	const FVector Out((float)DirX, (float)DirY, 0.f);
	const bool OutX = (DirX != 0);
	const float PlateW = Size * (Text.Len() <= 2 ? 1.7f : 2.6f);
	const float PlateH = Size * 1.6f;
	const FVector PlateSize = OutX ? FVector(3.f, PlateW, PlateH) : FVector(PlateW, 3.f, PlateH);
	if (Cube) { AddBox(Cube, PlateCenter + Out * 1.5f, PlateSize, FLinearColor(0.03f, 0.03f, 0.04f), false); }
	// Zelf-oplichtend nummer, ook 's nachts fel leesbaar.
	AddSignText(PlateCenter + Out * 4.f, DirX, DirY, Text, FLinearColor(1.f, 0.95f, 0.7f), Size, true);
}

void ACityGenerator::BuildCity()
{
	if (bBuilt) { return; }
	UWorld* W = GetWorld();
	if (!W) { return; }
	bBuilt = true;

	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!Cube) { return; }

	// Referentie-midden: de PlayerStart. BELANGRIJK voor co-op: de stad wordt LOKAAL per machine
	// gebouwd, dus host en client MOETEN exact dezelfde oorsprong kiezen. Bij meerdere PlayerStarts
	// is de iterator-volgorde niet gegarandeerd gelijk -> kies er deterministisch één (laagste naam).
	FVector Center = GetActorLocation();
	{
		APlayerStart* Chosen = nullptr;
		for (TActorIterator<APlayerStart> It(W); It; ++It)
		{
			APlayerStart* PS = *It;
			if (!Chosen || PS->GetName() < Chosen->GetName()) { Chosen = PS; }
		}
		if (Chosen) { Center = Chosen->GetActorLocation(); }
	}
	// Grond-hoogte DETERMINISTISCH uit de PlayerStart-hoogte (NIET via een trace): een trace kan op host
	// vs. client een andere hit geven -> de hele stad op een andere Z -> gerepliceerde spelers zweven of
	// vallen door de vloer. De PlayerStart is dezelfde actor op alle machines, dus dit is altijd gelijk.
	// (Capsule half-height = 96: de speler spawnt met z'n voeten op de vloer als PlayerStart 96 hoog staat.)
	GroundZ = Center.Z - 96.f;
	CityCenter = Center; // voor de kaart

	const float Pitch = BlockSize + RoadWidth; // hart-op-hart afstand tussen blokken
	const int32 R = FMath::Clamp(GridRadius, 1, 8);
	const float Span = (2 * R + 1) * Pitch;

	// The nav invoker controls tile generation, but the bounds volume decides where navmesh is allowed.
	// Keep the bounds larger than the full generated city so edge blocks get real paths too.
	{
		const float NavSpan = Span + BlockSize + RoadWidth;
		const FVector DesiredSize(NavSpan, NavSpan, 6200.f);
		const FVector DesiredCenter(Center.X, Center.Y, GroundZ + 2600.f);
		int32 UpdatedBounds = 0;
		for (TActorIterator<ANavMeshBoundsVolume> It(W); It; ++It)
		{
			ANavMeshBoundsVolume* Bounds = *It;
			if (!IsValid(Bounds))
			{
				continue;
			}

			const FBox CurrentBox = Bounds->GetComponentsBoundingBox(true);
			const FVector CurrentSize = CurrentBox.IsValid ? CurrentBox.GetSize() : FVector::ZeroVector;
			if (CurrentSize.X <= KINDA_SMALL_NUMBER || CurrentSize.Y <= KINDA_SMALL_NUMBER || CurrentSize.Z <= KINDA_SMALL_NUMBER)
			{
				continue;
			}

			if (UBrushComponent* Brush = Bounds->GetBrushComponent())
			{
				Brush->SetMobility(EComponentMobility::Movable);
			}

			Bounds->SetActorLocation(DesiredCenter);
			const FVector CurrentScale = Bounds->GetActorScale3D();
			Bounds->SetActorScale3D(FVector(
				CurrentScale.X * DesiredSize.X / CurrentSize.X,
				CurrentScale.Y * DesiredSize.Y / CurrentSize.Y,
				CurrentScale.Z * DesiredSize.Z / CurrentSize.Z));
			Bounds->ReregisterAllComponents();
			if (UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(W))
			{
				NavSys->OnNavigationBoundsUpdated(Bounds);
			}
			++UpdatedBounds;
		}

		if (UpdatedBounds > 0)
		{
			UE_LOG(LogWeedShop, Log, TEXT("City nav bounds resized: count=%d center=(%.0f, %.0f, %.0f) size=(%.0f, %.0f, %.0f)"),
				UpdatedBounds, DesiredCenter.X, DesiredCenter.Y, DesiredCenter.Z, DesiredSize.X, DesiredSize.Y, DesiredSize.Z);
		}
		else
		{
			UE_LOG(LogWeedShop, Warning, TEXT("City nav bounds resize skipped: no valid NavMeshBoundsVolume found for generated city"));
		}
	}

	// Eén centrale navigation-invoker die de HELE stad dekt -> runtime-navmesh overal, zonder dat elke
	// NPC z'n eigen invoker nodig heeft (schaalt veel beter naar 40+ NPC's).
	{
		AActor* NavAnchor = W->SpawnActor<AActor>(AActor::StaticClass(), FTransform(FVector(Center.X, Center.Y, GroundZ)));
		if (NavAnchor)
		{
			const float GenerationRadius = Span * 0.9f;
			const float RemovalRadius = Span * 1.05f;
			USceneComponent* AnchorRoot = NewObject<USceneComponent>(NavAnchor, TEXT("AnchorRoot"));
			NavAnchor->SetRootComponent(AnchorRoot);
			AnchorRoot->RegisterComponent();
			NavAnchor->SetActorLocation(FVector(Center.X, Center.Y, GroundZ));
			UNavigationInvokerComponent* Inv = NewObject<UNavigationInvokerComponent>(NavAnchor, TEXT("CityNavInvoker"));
			// Dek de HELE stad incl. hoeken: een hoekblok ligt op ~sqrt(2)*R*Pitch van het midden, wat
			// ruim buiten de oude 0.55*Span viel -> daar was geen navmesh en stonden NPC's vast. Royaal.
			Inv->SetGenerationRadii(GenerationRadius, RemovalRadius);
			Inv->RegisterComponent();
			if (UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(W))
			{
				NavSys->RegisterNavigationInvoker(NavAnchor, GenerationRadius, RemovalRadius);
				UE_LOG(LogWeedShop, Log, TEXT("City nav invoker registered: radius=(%.0f, %.0f) loc=(%.0f, %.0f, %.0f)"),
					GenerationRadius, RemovalRadius, Center.X, Center.Y, GroundZ);
			}
		}
	}

	// Rijweg-nav-areas: markeer elke straat (asfaltbaan tussen de blokken) als hoge-kosten "rijweg".
	// De stoepen (blok-plateaus) blijven default; de router verkiest dus de stoep en steekt alleen over
	// naar de volgende stoep i.p.v. midden over de weg te dwalen. XY-banen (geen Z-afhankelijkheid van
	// de lage stoeprand), dus stoep en straat raken elkaar niet.
	{
		const float ZHalf = 300.f;
		const float ZCenter = GroundZ + 50.f;
		const float HalfSpan = Span * 0.5f;
		const float HalfRoad = RoadWidth * 0.5f;
		auto SpawnRoad = [&](const FVector& Loc, const FVector& HalfExt)
		{
			if (ARoadNavZone* Z = W->SpawnActor<ARoadNavZone>(ARoadNavZone::StaticClass(), FTransform(Loc)))
			{
				Z->SetupZone(HalfExt);
			}
		};
		for (int32 k = -R; k < R; ++k) // straat tussen blok k en k+1
		{
			const float P = k * Pitch + Pitch * 0.5f;
			SpawnRoad(FVector(Center.X + P, Center.Y, ZCenter), FVector(HalfRoad, HalfSpan, ZHalf)); // verticale straat
			SpawnRoad(FVector(Center.X, Center.Y + P, ZCenter), FVector(HalfSpan, HalfRoad, ZHalf)); // horizontale straat
		}
	}

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

			// Layout: gas in het midden (naast het plein); grow/furniture/supplies VERSPREID over de map
			// (drie verschillende kwadranten, ver uit elkaar). De binnenring eromheen wordt woonbuurt.
			const int32 S = FMath::Max(2, R - 1); // spreid-afstand voor de winkels
			EShopKind Kind = EShopKind::Apartment;
			bool bLandmark = false;
			if (i == 0 && j == -1)       { Kind = EShopKind::GasStation; bLandmark = true; } // midden
			else if (i == S && j == S)   { Kind = EShopKind::Grow;       bLandmark = true; } // noordoost
			else if (i == -S && j == S)  { Kind = EShopKind::Furniture;  bLandmark = true; } // noordwest
			else if (i == S && j == -S)  { Kind = EShopKind::Supplies;   bLandmark = true; } // zuidoost

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

			// Het midden is een woonbuurt: de ring direct rond het plein is altijd rijtjeshuizen.
			if (FMath::Max(FMath::Abs(i), FMath::Abs(j)) == 1)
			{
				BuildRowHouses(CX, CY, TopZ, ddx, ddy, H);
				continue;
			}

			if ((H % 4u) != 0u) // ~3/4 -> rijtjeshuizen die het hele lot vullen
			{
				BuildRowHouses(CX, CY, TopZ, ddx, ddy, H);
				continue;
			}

			// Hoge flat (1/4 van de blokken): nu een echte flat MET interieur (trap, lift, units),
			// zonder neonbord -> de buitenkant blijft een gewone flat (doos + ramen + dakrand).
			const FLinearColor Body = CityFacade(H);
			const int32 Floors = 4 + (int32)((H >> 2) % 4);
			BuildApartmentBlock(CX, CY, TopZ, ddx, ddy, Floors, Body, Body, /*bSign*/ false);
		}
	}

	// Centraal parkje op het open middenblok.
	BuildPark(Center.X, Center.Y, BlockSize, GroundZ);

	// --- Echte top-down kaart: orthografische camera hoog boven het centrum, kijkt recht omlaag. ---
	{
		MapOrthoWidth = Span; // dekt de hele stad
		MapRT = NewObject<UTextureRenderTarget2D>(this, TEXT("MapRT"));
		MapRT->RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA8;
		MapRT->ClearColor = FLinearColor(0.04f, 0.05f, 0.07f, 1.f);
		MapRT->InitAutoFormat(1024, 1024);
		MapRT->UpdateResourceImmediate(true);

		MapCapture = NewObject<USceneCaptureComponent2D>(this, TEXT("MapCapture"));
		MapCapture->SetupAttachment(Root);
		MapCapture->RegisterComponent();
		MapCapture->ProjectionType = ECameraProjectionMode::Orthographic;
		MapCapture->OrthoWidth = MapOrthoWidth;
		MapCapture->TextureTarget = MapRT;
		MapCapture->bCaptureEveryFrame = false;
		MapCapture->bCaptureOnMovement = false;
		// Platte basiskleuren (albedo) i.p.v. de belichte scène -> vlakke, heldere kaart (wegen grijs,
		// gras groen, daken gekleurd) die altijd leesbaar is, ongeacht dag/nacht.
		MapCapture->CaptureSource = ESceneCaptureSource::SCS_BaseColor;
		// Pitch -90 + yaw 0 -> beeld: rechts = wereld +Y, omhoog = wereld +X (klopt met WorldToCanvas).
		MapCapture->SetWorldLocationAndRotation(FVector(Center.X, Center.Y, GroundZ + 60000.f), FRotator(-90.f, 0.f, 0.f));

		// Even wachten tot de net-gebouwde geometrie gerenderd is, dan 1x capturen.
		GetWorldTimerManager().SetTimer(MapCaptureTimer, this, &ACityGenerator::CaptureMapNow, 0.6f, false);
	}

	UE_LOG(LogWeedShop, Log, TEXT("City build complete: homes=%d blocks=%d lamps=%d"),
		ApartmentHomes.Num(), (2 * R + 1) * (2 * R + 1), LampLights.Num());
	if (UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(W))
	{
		const float NavSpan = Span + BlockSize + RoadWidth;
		const FVector DirtyCenter(Center.X, Center.Y, GroundZ + 1200.f);
		const FVector DirtyExtent(NavSpan * 0.5f, NavSpan * 0.5f, 2600.f);
		NavSys->AddDirtyArea(FBox(DirtyCenter - DirtyExtent, DirtyCenter + DirtyExtent), ENavigationDirtyFlag::All, TEXT("GeneratedCityComplete"));
		NavSys->Build();
		UE_LOG(LogWeedShop, Log, TEXT("City nav build requested after generated geometry"));
	}
	NavCoverageAttempts = 0;
	VerifyCityNavigationCoverage();
}

void ACityGenerator::CaptureMapNow()
{
	if (!MapCapture) { return; }

	// Karakters (NPC's + spelers) UIT de kaart-render houden: dit is een one-shot capture, dus anders
	// worden ze ingebakken op hun positie van dat moment en blijven ze als ghost-puntjes op de kaart
	// staan terwijl ze in het echt verder lopen. Live posities tekent de MapWidget zelf met losse dots.
	MapCapture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_RenderScenePrimitives;
	MapCapture->HiddenActors.Reset();
	if (UWorld* W = GetWorld())
	{
		for (TActorIterator<ACharacter> It(W); It; ++It)
		{
			if (IsValid(*It)) { MapCapture->HiddenActors.Add(*It); }
		}
	}
	MapCapture->CaptureScene();
}

void ACityGenerator::VerifyCityNavigationCoverage()
{
	UWorld* W = GetWorld();
	UNavigationSystemV1* Nav = W ? UNavigationSystemV1::GetCurrent(W) : nullptr;
	++NavCoverageAttempts;
	bNavCoverageRetryQueued = false;
	auto QueueCoverageRetry = [&]()
	{
		if (W && NavCoverageAttempts < 8)
		{
			bNavCoverageRetryQueued = true;
			NextNavCoverageRetryRealTime = W->GetRealTimeSeconds() + 3.f;
		}
	};
	if (!W || !Nav)
	{
		UE_LOG(LogWeedShop, Warning, TEXT("City nav coverage skipped: attempt=%d navigation system unavailable"), NavCoverageAttempts);
		QueueCoverageRetry();
		return;
	}

	TArray<FVector> Samples;
	TArray<bool> EdgeSample;
	const float Pitch = BlockSize + RoadWidth;
	const int32 R = FMath::Clamp(GridRadius, 1, 8);
	const int32 Open = FMath::Max(0, OpenPlazaRadius);
	const float Half = BlockSize * 0.5f;
	const float Lane = Half - SidewalkWidth * 0.5f;
	const float Along = FMath::Clamp(BlockSize * 0.28f, 320.f, 820.f);
	const float SampleZ = GroundZ + CurbHeight + 70.f;
	const FVector ProjectionExtent(420.f, 420.f, 900.f);

	auto AddSide = [&](int32 GX, int32 GY, int32 SideX, int32 SideY)
	{
		if (FMath::Max(FMath::Abs(GX), FMath::Abs(GY)) <= Open)
		{
			return;
		}
		const int32 NX = GX + SideX;
		const int32 NY = GY + SideY;
		if (NX < -R || NX > R || NY < -R || NY > R)
		{
			return;
		}

		const bool bEdge = FMath::Max(FMath::Abs(GX), FMath::Abs(GY)) == R;
		const FVector Base(CityCenter.X + GX * Pitch, CityCenter.Y + GY * Pitch, SampleZ);
		const FVector Normal = SideX != 0 ? FVector(static_cast<float>(SideX), 0.f, 0.f) : FVector(0.f, static_cast<float>(SideY), 0.f);
		const FVector Tangent = SideX != 0 ? FVector(0.f, 1.f, 0.f) : FVector(1.f, 0.f, 0.f);
		const float Offsets[] = { 0.f, Along, -Along };
		for (float Offset : Offsets)
		{
			Samples.Add(Base + Normal * Lane + Tangent * Offset);
			EdgeSample.Add(bEdge);
		}
	};

	for (int32 GX = -R; GX <= R; ++GX)
	{
		for (int32 GY = -R; GY <= R; ++GY)
		{
			AddSide(GX, GY, 1, 0);
			AddSide(GX, GY, -1, 0);
			AddSide(GX, GY, 0, 1);
			AddSide(GX, GY, 0, -1);
		}
	}

	TArray<FVector> Projected;
	TArray<bool> ProjectedIsEdge;
	Projected.Reserve(Samples.Num());
	ProjectedIsEdge.Reserve(Samples.Num());
	int32 EdgeTotal = 0;
	int32 EdgeProjected = 0;
	for (int32 i = 0; i < Samples.Num(); ++i)
	{
		if (EdgeSample[i])
		{
			++EdgeTotal;
		}
		FNavLocation NavLoc;
		if (Nav->ProjectPointToNavigation(Samples[i], NavLoc, ProjectionExtent))
		{
			Projected.Add(NavLoc.Location);
			ProjectedIsEdge.Add(EdgeSample[i]);
			if (EdgeSample[i])
			{
				++EdgeProjected;
			}
		}
	}

	if (Projected.Num() == 0)
	{
		UE_LOG(LogWeedShop, Warning, TEXT("City nav coverage failed: attempt=%d projected=0/%d edgeProjected=0/%d edgeReachable=0/0"),
			NavCoverageAttempts, Samples.Num(), EdgeTotal);
		QueueCoverageRetry();
		return;
	}

	int32 StartIndex = 0;
	float BestCenterDist = TNumericLimits<float>::Max();
	for (int32 i = 0; i < Projected.Num(); ++i)
	{
		const float Dist = FVector::DistSquared2D(Projected[i], CityCenter);
		if (Dist < BestCenterDist)
		{
			BestCenterDist = Dist;
			StartIndex = i;
		}
	}

	const FVector Start = Projected[StartIndex];
	TArray<FVector> EdgeGoals;
	EdgeGoals.Reserve(8);
	const FVector2D Dirs[] = {
		FVector2D(1.f, 0.f), FVector2D(-1.f, 0.f), FVector2D(0.f, 1.f), FVector2D(0.f, -1.f),
		FVector2D(1.f, 1.f), FVector2D(1.f, -1.f), FVector2D(-1.f, 1.f), FVector2D(-1.f, -1.f)
	};
	for (const FVector2D& RawDir : Dirs)
	{
		const FVector2D Dir = RawDir.GetSafeNormal();
		int32 BestIndex = INDEX_NONE;
		float BestScore = -TNumericLimits<float>::Max();
		for (int32 i = 0; i < Projected.Num(); ++i)
		{
			if (!ProjectedIsEdge[i])
			{
				continue;
			}
			const float Score = Projected[i].X * Dir.X + Projected[i].Y * Dir.Y;
			if (Score > BestScore)
			{
				BestScore = Score;
				BestIndex = i;
			}
		}
		if (BestIndex != INDEX_NONE)
		{
			EdgeGoals.AddUnique(Projected[BestIndex]);
		}
	}

	int32 EdgeReachable = 0;
	for (const FVector& Goal : EdgeGoals)
	{
		UNavigationPath* Path = UNavigationSystemV1::FindPathToLocationSynchronously(W, Start, Goal, this);
		const bool bReachable = Path && Path->IsValid() && !Path->IsPartial() && Path->PathPoints.Num() > 1;
		if (bReachable)
		{
			++EdgeReachable;
		}
	}

	const bool bWeakCoverage = Projected.Num() < Samples.Num() * 8 / 10
		|| (EdgeTotal > 0 && EdgeProjected < EdgeTotal * 8 / 10)
		|| EdgeGoals.Num() == 0
		|| EdgeReachable < EdgeGoals.Num();
	if (bWeakCoverage)
	{
		UE_LOG(LogWeedShop, Warning,
			TEXT("City nav coverage: attempt=%d projected=%d/%d edgeProjected=%d/%d edgeReachable=%d/%d"),
			NavCoverageAttempts, Projected.Num(), Samples.Num(), EdgeProjected, EdgeTotal, EdgeReachable, EdgeGoals.Num());
		QueueCoverageRetry();
	}
	else
	{
		UE_LOG(LogWeedShop, Log,
			TEXT("City nav coverage: attempt=%d projected=%d/%d edgeProjected=%d/%d edgeReachable=%d/%d"),
			NavCoverageAttempts, Projected.Num(), Samples.Num(), EdgeProjected, EdgeTotal, EdgeReachable, EdgeGoals.Num());
	}
}

void ACityGenerator::GetMapBlocks(TArray<FCityMapBlock>& Out) const
{
	Out.Reset();
	const float Pitch = BlockSize + RoadWidth;
	const int32 R = FMath::Clamp(GridRadius, 1, 8);
	const int32 Open = FMath::Max(0, OpenPlazaRadius);
	const int32 S = FMath::Max(2, R - 1);

	const FLinearColor CGas(0.92f, 0.32f, 0.26f), CGrow(0.36f, 0.85f, 0.40f), CFurn(0.72f, 0.52f, 0.92f),
		CSupp(0.40f, 0.70f, 1.0f), CApt(0.82f, 0.80f, 0.62f), CRow(0.80f, 0.62f, 0.42f), CPark(0.28f, 0.62f, 0.30f);

	for (int32 i = -R; i <= R; ++i)
	{
		for (int32 j = -R; j <= R; ++j)
		{
			FCityMapBlock B;
			B.Center = FVector2D(CityCenter.X + i * Pitch, CityCenter.Y + j * Pitch);

			if (FMath::Max(FMath::Abs(i), FMath::Abs(j)) <= Open)
			{
				B.Color = CPark; B.Label = TEXT("Park"); Out.Add(B); continue;
			}

			const uint32 H = CityHash(i, j);
			const int32 BaseNo = 2 + (int32)(CityHash((int32)(B.Center.X / 100.f), (int32)(B.Center.Y / 100.f)) % 70u) * 2;
			// Rijtjeshuizen: de losse huisnummers netjes naast elkaar (één spatie ertussen). Geen "-" in
			// dit label (anders ziet de kaart het als flat-reeks). De map toont dit als één nette chip.
			auto RowLabel = [&]() {
				const int32 Units = 3 + (int32)(H % 2u);
				const int32 RowBase = 2 + (int32)(H % 60u) * 2;
				FString S;
				for (int32 u = 0; u < Units; ++u) { if (u > 0) { S += TEXT(" "); } S += FString::FromInt(RowBase + 2 * u); }
				return S;
			};

			// Flat-label: gebouwnummer + de unit-reeks (bijv. "32  1-20"), zelfde nummering als de deuren binnen.
			auto AptLabel = [&]() {
				const float Foot = BlockSize - 2.f * SidewalkWidth;
				const float HallLen = Foot - 640.f;                 // CoreDepth = 640
				const int32 NApt = FMath::Max(2, (int32)(HallLen / 470.f));
				const int32 NF = 4 + (int32)((H >> 2) % 4u);        // verdiepingen (generieke flat)
				const int32 Total = NApt * 2 * NF;                  // units per zijde * 2 zijden * verdiepingen
				return FString::Printf(TEXT("%d  1-%d"), BaseNo, Total);
			};

			// LET OP: winkels eerst; de woon-toewijzing hieronder is 'else if' zodat de winkellabels
			// (GAS/GROW/...) NIET door huisnummers overschreven worden (dat was de bug: shop mét nummer).
			if (i == 0 && j == -1)      { B.Color = CGas;  B.Label = TEXT("GAS");       B.bShop = true; }
			else if (i == S && j == S)  { B.Color = CGrow; B.Label = TEXT("GROW");      B.bShop = true; }
			else if (i == -S && j == S) { B.Color = CFurn; B.Label = TEXT("FURNITURE"); B.bShop = true; }
			else if (i == S && j == -S) { B.Color = CSupp; B.Label = TEXT("SUPPLIES");  B.bShop = true; }
			else if (FMath::Max(FMath::Abs(i), FMath::Abs(j)) == 1) { B.Color = CRow; B.Label = RowLabel(); }
			else if ((H % 4u) != 0u)    { B.Color = CRow; B.Label = RowLabel(); }
			else                        { B.Color = CApt; B.Label = AptLabel(); }

			Out.Add(B);
		}
	}
}

void ACityGenerator::GetPropertyOffers(TArray<FCityPropertyOffer>& Out) const
{
	Out.Reset();
	const int32 N = ApartmentHomes.Num();
	if (N == 0) { return; }

	// Starter = flat-unit op de HOOGSTE verdieping (klein flatje bovenin).
	int32 StarterIdx = INDEX_NONE; int32 BestTop = -1;
	for (int32 i = 0; i < N; ++i)
	{
		const FApartmentHome& H = ApartmentHomes[i];
		if (H.bApartment && H.Floor > BestTop) { BestTop = H.Floor; StarterIdx = i; }
	}

	// Grote kamer = flat-unit op de laagste verdieping (ruim), niet de starter.
	int32 BigIdx = INDEX_NONE; int32 BestGround = INT32_MAX;
	for (int32 i = 0; i < N; ++i)
	{
		const FApartmentHome& H = ApartmentHomes[i];
		if (H.bApartment && i != StarterIdx && H.Floor <= BestGround) { BestGround = H.Floor; BigIdx = i; }
	}

	// Groepeer rijtjeshuizen per FYSIEK blok: aaneengesloten (opvolgende index) + dichtbij elkaar.
	// Zo kunnen we een huis uit een rij-VAN-4 onderscheiden van een huis uit een rij-VAN-3.
	TArray<TArray<int32>> RowBlocks;
	for (int32 i = 0; i < N; ++i)
	{
		if (ApartmentHomes[i].bApartment) { continue; }
		bool bSameBlock = false;
		if (RowBlocks.Num() > 0)
		{
			const int32 Prev = RowBlocks.Last().Last();
			bSameBlock = (Prev == i - 1) &&
				FVector::Dist2D(ApartmentHomes[Prev].InteriorPos, ApartmentHomes[i].InteriorPos) < 1200.f;
		}
		if (bSameBlock) { RowBlocks.Last().Add(i); }
		else { RowBlocks.Add(TArray<int32>{ i }); }
	}

	// Level-2 upgrade (EUR 15k) = een huis uit een rij van 4 (kleinere units). Laatste upgrade (EUR 60k)
	// = een huis uit een rij van 3 (ruimere units), in een ander blok.
	int32 RowIdx = INDEX_NONE;    // huis uit 4-rij
	int32 BigRowIdx = INDEX_NONE; // huis uit 3-rij
	for (const TArray<int32>& Blk : RowBlocks) { if (Blk.Num() >= 4) { RowIdx = Blk[1]; break; } }            // 2e huis van de 4-rij
	for (const TArray<int32>& Blk : RowBlocks) { if (Blk.Num() == 3) { BigRowIdx = Blk[1]; break; } }         // midden van de 3-rij
	// Fallbacks als een bloktype niet bestaat: pak gewoon een midden-huis uit een ander rij-blok.
	if (RowIdx == INDEX_NONE && RowBlocks.Num() > 0) { RowIdx = RowBlocks[0][RowBlocks[0].Num() / 2]; }
	if (BigRowIdx == INDEX_NONE)
	{
		for (const TArray<int32>& Blk : RowBlocks) { const int32 Mid = Blk[Blk.Num() / 2]; if (Mid != RowIdx) { BigRowIdx = Mid; break; } }
	}

	auto Add = [&](int32 Idx, const FString& Title, int64 Price, bool bStarter)
	{
		if (!ApartmentHomes.IsValidIndex(Idx)) { return; }
		const FApartmentHome& H = ApartmentHomes[Idx];
		FCityPropertyOffer O;
		O.Homes = { Idx };
		O.HomeIndex = Idx;
		O.Title = Title;
		O.Sub = H.bApartment
			? FString::Printf(TEXT("No. %s  -  floor %d"), *H.Number, H.Floor + 1)
			: FString::Printf(TEXT("Nr %s  -  rijtjeshuis"), *H.Number);
		O.PriceCents = Price;
		O.bStarter = bStarter;
		Out.Add(O);
	};

	if (StarterIdx != INDEX_NONE) { Add(StarterIdx, TEXT("Klein flatje (bovenin)"), 0, true); }
	if (RowIdx != INDEX_NONE)     { Add(RowIdx, TEXT("Terraced house (row of 4)"), 1500000, false); }   // EUR 15.000
	if (BigIdx != INDEX_NONE)     { Add(BigIdx, TEXT("Grote kamer (flat)"),      4000000, false); }   // EUR 40.000
	if (BigRowIdx != INDEX_NONE && BigRowIdx != RowIdx) { Add(BigRowIdx, TEXT("Terraced house (row of 3)"), 6000000, false); } // EUR 60.000
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

	const FLinearColor Roof(0.36f, 0.17f, 0.13f);
	const FLinearColor Seam(0.10f, 0.09f, 0.08f);
	const FLinearColor Grass(0.16f, 0.34f, 0.13f);
	const FLinearColor Path(0.60f, 0.58f, 0.52f);
	const FLinearColor Hedge(0.13f, 0.30f, 0.11f);

	// Voortuin-gras over de hele lot-breedte, vóór de gevels.
	const FVector YardC = FVector(CX, CY, TopZ + 3.f) + N * (Depth * 0.5f);
	const FVector YardSize = bAlongX ? FVector(YardDepth, RowLen, 6.f) : FVector(RowLen, YardDepth, 6.f);
	AddBox(Cube, YardC, YardSize, Grass, false);

	const int32 RowBase = 2 + (int32)(Seed % 60u) * 2; // even straatnummer; opvolgende huizen +2

	for (int32 u = 0; u < Units; ++u)
	{
		const uint32 hu = CityHash((int32)Seed + u * 101 + 9, u * 53 + 17);
		const float Along = -RowLen * 0.5f + (u + 0.5f) * UnitLen;
		const float UX = BCX + Tt.X * Along;
		const float UY = BCY + Tt.Y * Along;
		const FLinearColor Body = CityFacade(hu);

		// Holle 2-verdiepingen-woning met echte ramen (voor/achtergevel), party-muren tot het dak,
		// 2e vloer, rechte trap en werkende deur.
		ACityDoor* UnitDoor = BuildHouseUnitInterior(UX, UY, Depth, UnitLen - 4.f, WallH, bAlongX, bAlongX ? Ddx : Ddy, TopZ, Body);

		// Huisnummer op een bordje RECHTS naast de voordeur (NL-stijl), op ooghoogte, kijkend naar de straat.
		{
			const FVector Door = FVector(UX, UY, TopZ) + N * (Depth * 0.5f);
			const FVector Plate = FVector(Door.X, Door.Y, TopZ + 145.f) + N * 9.f + Tt * 62.f;
			AddDoorNumber(Plate, Ddx, Ddy, FString::FromInt(RowBase + 2 * u), 18.f);
		}

		// Registreer dit rijtjeshuis als woning (deur + plek vóór de deur + huisnummer).
		{
			const float FrontD = FootMax * 0.5f + SidewalkWidth * 0.5f; // midden op de stoep
			const FVector Front = FVector(CX, CY, TopZ + 8.f) + N * FrontD + Tt * Along;
			FApartmentHome H;
			H.Door = UnitDoor;
			H.InteriorPos = FVector(UX, UY, TopZ + 8.f);
			H.DoorPos = Front;
			H.Number = FString::FromInt(RowBase + 2 * u);
			H.bApartment = false; H.Floor = 0;
			// Kamer-grenzen: langs de rij = UnitLen, in de diepte = Depth; hoogte = beide verdiepingen.
			H.RoomHalf = bAlongX ? FVector(Depth * 0.5f, UnitLen * 0.5f, WallH)
								  : FVector(UnitLen * 0.5f, Depth * 0.5f, WallH);
			ApartmentHomes.Add(H);
		}

		// Looppad van de deur door de voortuin tot MIDDEN op de stoep (niet door tot in de straat).
		const float PathBack = Depth * 0.5f - YardDepth * 0.5f;          // bij de voordeur
		const float PathFront = FootMax * 0.5f + SidewalkWidth * 0.5f;   // midden op de stoep, vóór de straat
		const float PathLen = PathFront - PathBack;
		const FVector PathC = FVector(CX, CY, TopZ + 4.f) + N * ((PathBack + PathFront) * 0.5f) + Tt * Along;
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

void ACityGenerator::BuildApartmentBlock(float CX, float CY, float TopZ, int32 Ddx, int32 Ddy, int32 Floors, const FLinearColor& Body, const FLinearColor& Sign, bool bSign)
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
	const float DoorH = 220.f;
	const FLinearColor FloorC(0.30f, 0.29f, 0.27f);

	const int32 DoorSide = (Ddx > 0) ? 0 : (Ddx < 0) ? 1 : (Ddy > 0) ? 2 : 3;
	const FVector N((float)Ddx, (float)Ddy, 0.f);

	// Buitenmuren met ECHTE ramen (glasstrook per verdieping); de straatgevel heeft op de begane grond
	// een deur-opening. Length = volle gevel incl. hoeken.
	const float WallLen = Foot + 2.f * T;
	BuildWallWindows(CX + Half + T * 0.5f, CY, false, WallLen, TopZ, NF, FloorH, T, Body, CY, DoorSide == 0 ? DoorW : 0.f, DoorH); // +X
	BuildWallWindows(CX - Half - T * 0.5f, CY, false, WallLen, TopZ, NF, FloorH, T, Body, CY, DoorSide == 1 ? DoorW : 0.f, DoorH); // -X
	BuildWallWindows(CX, CY + Half + T * 0.5f, true,  WallLen, TopZ, NF, FloorH, T, Body, CX, DoorSide == 2 ? DoorW : 0.f, DoorH); // +Y
	BuildWallWindows(CX, CY - Half - T * 0.5f, true,  WallLen, TopZ, NF, FloorH, T, Body, CX, DoorSide == 3 ? DoorW : 0.f, DoorH); // -Y

	// === Ingang-georiënteerd interieur: ingang -> middengang -> appartementen beide kanten -> trap links-achter + lift rechts-achter ===
	const bool NX = (Ddx != 0);
	const FVector Fwd = -N;                          // de bouw in (van ingang naar achter)
	const FVector Side(-N.Y, N.X, 0.f);              // dwars op de gang
	auto LP = [&](float d, float s) -> FVector { return FVector(CX, CY, 0.f) + N * (Half - d) + Side * s; }; // (d,s)->wereld XY
	auto Box = [&](float dC, float sC, float dLen, float sLen, float zC, float h, const FLinearColor& Col, bool Coll)
	{
		const FVector c = LP(dC, sC);
		AddBox(Cube, FVector(c.X, c.Y, zC), NX ? FVector(dLen, sLen, h) : FVector(sLen, dLen, h), Col, Coll);
	};

	const float HallW = 280.f, HW = HallW * 0.5f;
	const float CoreDepth = 640.f;                   // achterste zone (trap links, lift rechts)
	const float HallLen = Foot - CoreDepth;          // gang van de ingang tot de kern
	const float SideW = Half - HW;                   // breedte van een appartement-zone per kant
	const float StairLaneWd = FMath::Min(240.f, CoreDepth * 0.42f); // trap-loopbreedte (langs diepte)
	const float StairDoorC = HallLen + StairLaneWd * 0.5f + 20.f;   // instap (voorste trap) = deuropening 1
	const float StairBackC = Foot - StairLaneWd * 0.5f - 20.f;      // uitstap (achterste trap) = deuropening 2
	const float WallT = 16.f, WallHt = FloorH, DoorTopH = 205.f, DoorGap = 110.f; // muren helemaal tot het plafond (geen gat)
	const FLinearColor IWall(0.60f, 0.58f, 0.54f), ADoorC(0.30f, 0.20f, 0.12f);
	const FLinearColor StepC = Body * 0.7f, LandC = Body * 0.5f;
	const int32 NApt = FMath::Max(2, (int32)(HallLen / 470.f));
	const float AptLen = HallLen / NApt;
	const float LiftCabD = HallLen + CoreDepth * 0.5f; // d-positie van de liftcabine + de doorgang ernaartoe
	const int32 BaseNo = 2 + (int32)(CityHash((int32)(CX / 100.f), (int32)(CY / 100.f)) % 70u) * 2; // even gebouwnummer
	int32 AptSeq = 0; // doorlopende appartement-nummering (BaseNo-1, BaseNo-2, ...)

	// ---- Vloeren: middengang (volle diepte) + appartementen (zijkanten, tot de kern). Trap/lift achterin = gat. ----
	for (int32 f = 1; f < NF; ++f)
	{
		const float z = TopZ + f * FloorH;
		Box(Foot * 0.5f, 0.f, Foot + 2.f * WallT, HallW, z, 12.f, FloorC, true);
		Box(HallLen * 0.5f, -(HW + SideW * 0.5f), HallLen, SideW + WallT, z, 12.f, FloorC, true);
		Box(HallLen * 0.5f,  (HW + SideW * 0.5f), HallLen, SideW + WallT, z, 12.f, FloorC, true);
		const FVector L0 = LP(HallLen * 0.6f, 0.f); AddInteriorLight(FVector(L0.X, L0.Y, z - 55.f));
	}
	AddBox(Cube, FVector(CX, CY, TopZ + NF * FloorH), FVector(Foot, Foot, 14.f), FloorC, true); // dichte dakvloer
	AddInteriorLight(FVector(CX, CY, TopZ + NF * FloorH - 55.f));

	// ---- Switchback-trap links-achter (banen langs de diepte, bordes achteraan). ----
	{
		const int32 SPF = 14;
		const float HalfRise = FloorH * 0.5f, Rise = HalfRise / SPF;
		const float Margin = 25.f;
		const float RunS = (SideW - 2.f * Margin) / SPF;          // trede-diepte LANGS DE INSTAPRICHTING (Side)
		const float LaneWd = StairLaneWd;                         // loopbreedte langs de diepte
		const float dFront = StairDoorC;                          // baan dichtbij de gang (= instap/deuropening)
		const float dBack = Foot - LaneWd * 0.5f - 20.f;          // baan achterin
		const float s0 = -HW - Margin;                            // begint bij de gang-instap
		// Smalle drempel op vloerniveau aan de gangkant per verdieping (volle diepte) om af te stappen.
		for (int32 f = 0; f < NF; ++f)
		{
			const float zf = TopZ + f * FloorH;
			Box(HallLen + CoreDepth * 0.5f, -HW - 28.f, CoreDepth, 64.f, zf - 6.f, 12.f, LandC, true);
		}
		for (int32 f = 0; f < NF - 1; ++f)
		{
			const float zf = TopZ + f * FloorH;
			// Steek 1 (baan vooraan): van de gang (-HW) de schacht in (-Side), omhoog naar halve hoogte.
			for (int32 k = 0; k < SPF; ++k) { const float tt = zf + (k + 1) * Rise; Box(dFront, s0 - (k + 0.5f) * RunS, LaneWd, RunS + 5.f, tt - 6.f, 12.f, StepC, true); }
			// Bordes aan de muur/raam-kant, verbindt beide banen. Dieper gemaakt zodat de overloop ruim is.
			Box((dFront + dBack) * 0.5f, -Half + Margin + RunS * 0.4f, FMath::Abs(dBack - dFront) + LaneWd, RunS * 2.8f, zf + HalfRise - 8.f, 16.f, LandC, true);
			// Steek 2 (baan achterin): terug naar de gang, omhoog naar de volgende verdieping.
			for (int32 k = 0; k < SPF; ++k) { const float tt = zf + HalfRise + (k + 1) * Rise; Box(dBack, -Half + Margin + (k + 0.5f) * RunS, LaneWd, RunS + 5.f, tt - 6.f, 12.f, StepC, true); }
			// Dikke blok in het midden tussen de twee trapdelen (vol, op verdieping-hoogte) zodat je niet door het
			// midden naar beneden kunt springen. Het bordes achteraan blijft vrij voor de doorloop.
			const float GapD = FMath::Abs(dBack - dFront) - LaneWd;
			if (GapD > 8.f)
			{
				const float dMid = (dFront + dBack) * 0.5f;
				const float sLandFront = -Half + Margin + RunS * 3.0f; // balk dunner aan de muur/raam-kant -> ruimere overloop
				const float sHall = -HW + 8.f;                         // loopt door tot de gang-tussenmuur (geen gat meer)
				const float sDivC = (sHall + sLandFront) * 0.5f;
				const float sDivLen = FMath::Abs(sHall - sLandFront);
				Box(dMid, sDivC, GapD + 16.f, sDivLen, zf + FloorH * 0.5f, FloorH + 24.f, FLinearColor(0.17f, 0.17f, 0.20f), true);
			}
		}
		// Bovenste verdieping heeft geen trap omhoog -> toch de dikke blok in het midden, helemaal tot het plafond.
		{
			const float zTop = TopZ + (NF - 1) * FloorH;
			const float dMid = (dFront + dBack) * 0.5f;
			const float GapD = FMath::Abs(dBack - dFront) - LaneWd;
			const float sLandFront = -Half + Margin + RunS * 3.0f;
			const float sHall = -HW + 8.f;
			if (GapD > 8.f)
			{
				const float sDivC = (sHall + sLandFront) * 0.5f;
				const float sDivLen = FMath::Abs(sHall - sLandFront);
				Box(dMid, sDivC, GapD + 16.f, sDivLen, zTop + FloorH * 0.5f, FloorH + 24.f, FLinearColor(0.17f, 0.17f, 0.20f), true);
			}
			// Dicht de open trapschacht-vloer op de bovenste verdieping zodat je hier niet 1 verdieping naar
			// beneden kunt springen. Alleen de afdaal-kolom (dBack) blijft open om de trap af te lopen.
			{
				const float sZoneC = (-HW - Half) * 0.5f;
				const float sZoneLen = FMath::Abs(Half - HW);
				const float backOpen0 = dBack - LaneWd * 0.5f;
				const float backOpen1 = dBack + LaneWd * 0.5f;
				if (backOpen0 > HallLen + 4.f) { Box((HallLen + backOpen0) * 0.5f, sZoneC, backOpen0 - HallLen, sZoneLen, zTop - 6.f, 12.f, LandC, true); }
				if (Foot > backOpen1 + 4.f) { Box((backOpen1 + Foot) * 0.5f, sZoneC, Foot - backOpen1, sZoneLen, zTop - 6.f, 12.f, LandC, true); }
			}
		}
	}

	// ---- Per verdieping: lamp bij de lift/hal (donker als de lift weg is) + "LEVEL N"-bordje op de trap-pilaar. ----
	{
		const float dFrontS = StairDoorC;
		const float dBackS = Foot - StairLaneWd * 0.5f - 20.f;
		const float dMidS = (dFrontS + dBackS) * 0.5f;                 // midden van de dikke blok / gang-tussenmuur
		const float sPlate = -HW + 12.f;                              // hal-kant van de WITTE gang-tussenmuur (s = -HW)
		const int32 WDirX = FMath::RoundToInt(Side.X), WDirY = FMath::RoundToInt(Side.Y); // bordje de gang in gericht
		const float SignSize = 26.f;
		const float PlateW = SignSize * 4.6f;                         // breed genoeg voor "LEVEL N"
		for (int32 f = 0; f < NF; ++f)
		{
			const float zf = TopZ + f * FloorH;
			// Lamp aan de gang-kant bij de lift (elke verdieping), zodat het er niet donker is als de lift weg is.
			const FVector LiftLamp = LP(HallLen - 30.f, HW * 0.6f);
			AddInteriorLight(FVector(LiftLamp.X, LiftLamp.Y, zf + FloorH - 60.f));
			// Bordje (donkere plaat + oplichtende "LEVEL N") OP de witte gang-tussenmuur (hal-kant), de gang in gericht.
			Box(dMidS, sPlate, PlateW, 6.f, zf + 178.f, SignSize * 1.7f, FLinearColor(0.03f, 0.03f, 0.05f), false);
			const FVector SignPos = LP(dMidS, sPlate + 5.f);
			AddSignText(FVector(SignPos.X, SignPos.Y, zf + 178.f), WDirX, WDirY,
				FString::Printf(TEXT("LEVEL %d"), f + 1), FLinearColor(0.6f, 0.9f, 1.f), SignSize, true);
		}
	}

	// ---- Lift rechts-achter: cabine docked aan de gangkant, schuifdeur naar de gang gericht. ----
	{
		const float CabSize = FMath::Min(SideW - 20.f, 300.f);
		const FVector P = LP(LiftCabD, HW + CabSize * 0.5f);
		const float YawE = FMath::RadiansToDegrees(FMath::Atan2(-N.Y, -N.X)); // schuifdeur richting de gang (-Side)
		FActorSpawnParameters SP; SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		if (ACityElevator* Ele = W->SpawnActor<ACityElevator>(ACityElevator::StaticClass(), FTransform(FRotator(0.f, YawE, 0.f), FVector(P.X, P.Y, TopZ)), SP))
		{
			Ele->Setup(TopZ, FloorH, NF, CabSize, CabSize, Body * 0.5f);
		}
	}

	// ---- Binnenmuren per verdieping: gang-zijwanden (appartementdeuren + doorgang naar schacht) + partities + achterwanden. ----
	{
		// Muur langs de diepte (d-as) op vaste s; thickness langs Side.
		auto SegD = [&](float d0, float d1, float s, float zS) { if (FMath::Abs(d1 - d0) > 4.f) { Box((d0 + d1) * 0.5f, s, FMath::Abs(d1 - d0), WallT, zS + WallHt * 0.5f, WallHt, IWall, true); } };
		// Muur langs de breedte (s-as) op vaste d; thickness langs Fwd.
		auto SegS = [&](float s0, float s1, float d, float zS) { if (FMath::Abs(s1 - s0) > 4.f) { Box(d, (s0 + s1) * 0.5f, WallT, FMath::Abs(s1 - s0), zS + WallHt * 0.5f, WallHt, IWall, true); } };
		auto LintelD = [&](float dC, float s, float zS) { const float h = WallHt - DoorTopH; if (h > 4.f) { Box(dC, s, DoorGap, WallT, zS + DoorTopH + h * 0.5f, h, IWall, true); } };
		// Werkende deur in een d-wand (paneel langs de diepte; scharnier aan de -Fwd-kant van het gat).
		auto DoorD = [&](float dC, float s, float zS) -> ACityDoor*
		{
			const FVector hinge = LP(dC - DoorGap * 0.5f, s) + FVector(0.f, 0.f, zS);
			const float yaw = FMath::RadiansToDegrees(FMath::Atan2(Fwd.Y, Fwd.X));
			FActorSpawnParameters DSP; DSP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			ACityDoor* Dr = W->SpawnActor<ACityDoor>(ACityDoor::StaticClass(), FTransform(FRotator(0.f, yaw, 0.f), hinge), DSP);
			if (Dr) { Dr->Setup(DoorGap - 4.f, DoorTopH - 6.f, ADoorC); }
			return Dr;
		};
		// Deurkozijn rond een opening in een d-wand: stijlen (jambs) + bovendorpel, iets dikker dan de muur
		// zodat de blote muur-dikte-rand niet meer zichtbaar is en de opening netjes "aansluit".
		auto DoorFrameD = [&](float dC, float s, float zS, float openW, float topH)
		{
			const float JambW = 9.f, Proud = WallT + 6.f, zc = zS + topH * 0.5f;
			Box(dC - openW * 0.5f, s, JambW, Proud, zc, topH, ADoorC, true);                 // linker stijl
			Box(dC + openW * 0.5f, s, JambW, Proud, zc, topH, ADoorC, true);                 // rechter stijl
			// Bovendorpel: vult het gat boven de deur (deur is iets lager dan de opening) -> geen doorkijk meer.
			Box(dC, s, openW + 2.f * JambW, Proud, zS + topH - 5.f, 18.f, ADoorC, true);
		};

		for (int32 f = 0; f < NF; ++f)
		{
			const float zS = TopZ + f * FloorH;
			// Begane grond: één unit minder per zijde -> bredere "grote kamer"-units, zodat de Grote-kamer-
			// offer (begane-grond unit) ook echt ruimer is dan een gewone appartement-unit hierboven.
			const int32 NAptF = (f == 0) ? FMath::Max(1, NApt - 1) : NApt;
			const float AptLenF = HallLen / NAptF;
			for (int32 side = -1; side <= 1; side += 2)
			{
				const float sw = side * HW;
				// Voorste deel: appartementdeur per unit.
				float cur = 0.f;
				for (int32 a = 0; a < NAptF; ++a)
				{
					const float aCenter = (a + 0.5f) * AptLenF;
					SegD(cur, aCenter - DoorGap * 0.5f, sw, zS);
					cur = aCenter + DoorGap * 0.5f;
					LintelD(aCenter, sw, zS);
					ACityDoor* AptDoor = DoorD(aCenter, sw, zS);
					DoorFrameD(aCenter, sw, zS, DoorGap, DoorTopH); // net kozijn -> geen blote muur-rand

					// Plafondlamp midden in elk appartement.
					const FVector LAp = LP(aCenter, side * (HW + SideW * 0.5f));
					AddInteriorLight(FVector(LAp.X, LAp.Y, zS + FloorH - 55.f));
					// Huisnummer-bordje RECHTS naast de appartementdeur (BaseNo-seq), op ooghoogte, kijkend de gang in.
					++AptSeq;
					const FVector DrXY = LP(aCenter + DoorGap * 0.5f + 20.f, sw);
					const FVector NumDir((float)(side * Ddy), (float)(-side * Ddx), 0.f);
					const FVector PlateLoc = FVector(DrXY.X, DrXY.Y, zS + 135.f) + NumDir * (WallT * 0.5f + 2.f);
					AddDoorNumber(PlateLoc, side * Ddy, -side * Ddx, FString::Printf(TEXT("%d-%d"), BaseNo, AptSeq), 15.f);
					// Registreer dit appartement als 'home'. De bewoner gaat 's nachts naar de plek VÓÓR de
					// voordeur van het gebouw (en verdwijnt daar 'naar binnen'); z'n appartementdeur binnen
					// gaat op slot met z'n naam. (AI kan niet door de voordeur, dus geen binnen-pathing nodig.)
					{
						const FVector Inside = LP(aCenter, side * (HW + SideW * 0.5f)); // echt het midden van de unit (kamer-box centreren)
						const FVector Hall = LP(aCenter, side * (HW - 70.f));
						const FVector FrontSpot = FVector(CX, CY, TopZ + 8.f) + N * (Half + SidewalkWidth * 0.5f);
						FApartmentHome H;
						H.Door = AptDoor;
						H.InteriorPos = FVector(Inside.X, Inside.Y, zS + 8.f);
						H.HallPos = FVector(Hall.X, Hall.Y, zS + 8.f);
						H.DoorPos = FrontSpot;
						H.Number = FString::Printf(TEXT("%d-%d"), BaseNo, AptSeq);
						H.bApartment = true; H.Floor = f;
						// Kamer-grenzen: langs de gang = AptLenF (begane grond = breder), in de diepte = SideW.
						H.RoomHalf = NX ? FVector(AptLenF * 0.5f, SideW * 0.5f, FloorH)
										: FVector(SideW * 0.5f, AptLenF * 0.5f, FloorH);
						ApartmentHomes.Add(H);
					}
				}
				SegD(cur, HallLen, sw, zS); // gang-zijwand tot de kern
				if (side > 0)
				{
					// Lift-kant: dichte wand met een doorgang precies zo breed als de lift-deuropening
					// (de schachtdeuren vullen dit gat; de bredere cabine zit erachter verstopt).
					const float Cab = FMath::Min(SideW - 20.f, 300.f);
					const float OpenW = FMath::Clamp((Cab - 80.f) * 0.5f, 40.f, Cab * 0.24f); // bladen passen in de pocket (geen muur-glitch)
					const float LiftOpen = 2.f * OpenW + 8.f;
					SegD(HallLen, LiftCabD - LiftOpen * 0.5f, sw, zS);
					SegD(LiftCabD + LiftOpen * 0.5f, Foot, sw, zS);
					// Latei boven de lift-opening: schacht is niet meer open bóven de deuren.
					const float LintH = WallHt - 210.f;
					if (LintH > 4.f) { Box(LiftCabD, sw, LiftOpen, WallT, zS + 210.f + LintH * 0.5f, LintH, IWall, true); }
					DoorFrameD(LiftCabD, sw, zS, LiftOpen, 210.f); // lift-kozijn -> nette opening, geen blote rand
				}
				else
				{
					// Trap-kant: TWEE deuropeningen -> één bij de instap (voorste trap) en één bij de uitstap
					// (achterste trap), zodat je na het omhoog lopen recht de gang in stapt i.p.v. tegen de muur.
					const float SDW = StairLaneWd + 30.f;
					SegD(StairDoorC + SDW * 0.5f, StairBackC - SDW * 0.5f, sw, zS); // muur tussen de twee openingen
					SegD(StairBackC + SDW * 0.5f, Foot, sw, zS);                    // muurtje na de tweede opening
				}
				// Achterwand van de appartementen (sluit ze af van de schacht-zone).
				SegS(side * HW, side * Half, HallLen, zS);
				// Partities tussen de appartementen.
				for (int32 a = 1; a < NAptF; ++a) { SegS(side * HW, side * Half, a * AptLenF, zS); }
			}
		}
	}

	// Legenda/bewonerslijst in de entree-hal: welke woningen op welke verdieping. Op de linkerwand (s=-HW)
	// vlak bij de ingang, kijkend de gang in -> je ziet 'm meteen als je binnenkomt.
	{
		const int32 PF = 2 * NApt;                                   // woningen per verdieping
		const float Avail = AptLen * 0.5f - DoorGap * 0.5f - 20.f;   // dichte wand vóór de eerste deur
		const float BoardW = FMath::Clamp(Avail, 80.f, 160.f);
		const float dBoard = BoardW * 0.5f + 14.f;
		const float sFace = -HW + WallT * 0.5f;                      // gang-zijde van de linkerwand
		const int32 NLines = NF + 1;
		const float Lh = 18.f;
		const float BoardH = NLines * Lh + 18.f;
		const float Zc = TopZ + 160.f;
		Box(dBoard, sFace + 2.5f, BoardW, 5.f, Zc, BoardH, FLinearColor(0.03f, 0.03f, 0.05f), false);
		const int32 DX = -Ddy, DY = Ddx;                             // kijkt de gang in (+Side)
		const FVector TextXY = LP(dBoard, sFace + 6.f);
		auto LineZ = [&](int32 i) { return Zc + BoardH * 0.5f - 13.f - i * Lh; };
		AddSignText(FVector(TextXY.X, TextXY.Y, LineZ(0)), DX, DY, FString::Printf(TEXT("Nr %d"), BaseNo), FLinearColor(0.6f, 0.85f, 1.f), 13.f, true);
		for (int32 f = 0; f < NF; ++f)
		{
			const int32 lo = f * PF + 1, hi = f * PF + PF;
			AddSignText(FVector(TextXY.X, TextXY.Y, LineZ(f + 1)), DX, DY,
				FString::Printf(TEXT("%d: %d-%d t/m %d"), f + 1, BaseNo, lo, hi), FLinearColor(1.f, 0.95f, 0.7f), 11.f, true);
		}
	}

	// Plat dak + parapet.
	AddBox(Cube, FVector(CX, CY, TopZ + TotalH + 10.f), FVector(Foot + 16.f, Foot + 16.f, 20.f), Body * 0.6f, false);

	// Donker naambord + gloeiende neon-tekst + neonlicht (alleen voor echte appartement-landmarks).
	if (bSign)
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

	// Werkende DUBBELE voordeur: twee losse helften die elk apart open/dicht gaan (apart interacten).
	{
		const FVector Tang(-N.Y, N.X, 0.f);
		const FVector OpeningCenter = FVector(CX, CY, TopZ) + N * (Half - 2.f);
		const float HalfW = DoorW * 0.5f;
		const float Yaw = FMath::RadiansToDegrees(FMath::Atan2(Tang.Y, Tang.X));
		const FLinearColor DoorCol(0.10f, 0.08f, 0.07f);
		FActorSpawnParameters DSP; DSP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		// Linker helft: scharnier aan de linkerkant, paneel loopt naar het midden (+Tang).
		const FVector HingeL = OpeningCenter - Tang * HalfW;
		if (ACityDoor* DL = W->SpawnActor<ACityDoor>(ACityDoor::StaticClass(), FTransform(FRotator(0.f, Yaw, 0.f), HingeL), DSP))
		{
			DL->Setup(HalfW - 4.f, DoorH - 6.f, DoorCol);
		}
		// Rechter helft: scharnier aan de rechterkant, 180 gedraaid zodat het paneel naar het midden (-Tang) loopt.
		const FVector HingeR = OpeningCenter + Tang * HalfW;
		if (ACityDoor* DR = W->SpawnActor<ACityDoor>(ACityDoor::StaticClass(), FTransform(FRotator(0.f, Yaw + 180.f, 0.f), HingeR), DSP))
		{
			DR->Setup(HalfW - 4.f, DoorH - 6.f, DoorCol);
		}
	}

	// Gebouwnummer op een bordje RECHTS naast de voordeur (NL-stijl), op ooghoogte, kijkend naar de straat.
	{
		const FVector Tang(-N.Y, N.X, 0.f);
		const FVector Plate = FVector(CX, CY, TopZ + 150.f) + N * (Half + T + 5.f) + Tang * (DoorW * 0.5f + 32.f);
		AddDoorNumber(Plate, Ddx, Ddy, FString::FromInt(BaseNo), 26.f);
	}
}

void ACityGenerator::AddInteriorLight(const FVector& WorldLoc)
{
	// Echt plafondlampje (kapje + gloeiende bol) i.p.v. een licht-uit-het-niets.
	UStaticMesh* Cone = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cone.Cone"));
	UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (Cone) { AddBox(Cone, WorldLoc + FVector(0.f, 0.f, 10.f), FVector(34.f, 34.f, 26.f), FLinearColor(0.07f, 0.07f, 0.09f), false, FRotator(180.f, 0.f, 0.f)); }
	if (Sphere) { AddBox(Sphere, WorldLoc + FVector(0.f, 0.f, -6.f), FVector(15.f, 15.f, 15.f), FLinearColor(1.f, 0.9f, 0.6f), false); }

	UPointLightComponent* PL = NewObject<UPointLightComponent>(this);
	PL->SetupAttachment(Root);
	PL->RegisterComponent();
	PL->SetWorldLocation(WorldLoc + FVector(0.f, 0.f, -28.f));
	PL->SetMobility(EComponentMobility::Movable);
	PL->SetAttenuationRadius(900.f);
	PL->SetIntensity(6000.f);
	PL->SetLightColor(FLinearColor(1.f, 0.92f, 0.78f));
	PL->SetCastShadows(false);
}

ACityDoor* ACityGenerator::BuildHouseUnitInterior(float UX, float UY, float D, float L, float WallH, bool bAlongX, int32 Ndir, float TopZ, const FLinearColor& Body)
{
	UWorld* W = GetWorld();
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!W || !Cube) { return nullptr; }

	const float FloorH = WallH * 0.5f;
	const float T = 14.f;
	const float HD = D * 0.5f, HL = L * 0.5f;
	const float DoorW = 95.f, DoorH = 205.f;
	const FLinearColor FloorC(0.32f, 0.30f, 0.27f);
	const FLinearColor StepC = Body * 0.7f;
	const FLinearColor DoorC(0.30f, 0.20f, 0.12f);

	const FVector N = bAlongX ? FVector((float)Ndir, 0.f, 0.f) : FVector(0.f, (float)Ndir, 0.f); // naar de straat
	const FVector Tt = bAlongX ? FVector(0.f, 1.f, 0.f) : FVector(1.f, 0.f, 0.f);                // langs de rij
	const FVector Base(UX, UY, 0.f);

	const float RidgeH = D * 0.5f;            // moet matchen met het rij-dak (Depth*0.5)
	const bool WAlongX = (Tt.X != 0.f);       // voor/achtergevel loopt langs Tt
	const float DoorCenterAxis = WAlongX ? UX : UY;

	// Voorgevel (straat, met deur) + achtergevel (tuin) met ECHTE ramen (glasstrook per verdieping).
	const FVector FC = Base + N * HD;
	BuildWallWindows(FC.X, FC.Y, WAlongX, L, TopZ, 2, FloorH, T, Body, DoorCenterAxis, DoorW, DoorH);
	const FVector BkC = Base - N * HD;
	BuildWallWindows(BkC.X, BkC.Y, WAlongX, L, TopZ, 2, FloorH, T, Body, 0.f, 0.f, DoorH);

	// Zijwanden = party walls: VOL tot de goot + een gable-driehoek tot het DAK, zodat elke woning
	// een eigen, tot het dak afgesloten ruimte is (niet langer 1 open zolder over de hele rij).
	auto SideWallGable = [&](const FVector& sideC)
	{
		const float sx = bAlongX ? D : T, sy = bAlongX ? T : D;
		AddBox(Cube, FVector(sideC.X, sideC.Y, TopZ + WallH * 0.5f), FVector(sx, sy, WallH), Body, true);
		const int32 NSl = 12; const float SliceW = D / NSl; const float HalfDp = D * 0.5f;
		for (int32 k = 0; k < NSl; ++k)
		{
			const float n = -HalfDp + (k + 0.5f) * SliceW;
			const float inner = FMath::Max(0.f, FMath::Abs(n) - SliceW * 0.5f);
			const float hh = RidgeH * (1.f - inner / HalfDp);
			if (hh < 6.f) { continue; }
			const FVector P = sideC + N * n;
			const float gx = bAlongX ? (SliceW + 2.f) : T, gy = bAlongX ? T : (SliceW + 2.f);
			AddBox(Cube, FVector(P.X, P.Y, TopZ + WallH + hh * 0.5f), FVector(gx, gy, hh), Body, true);
		}
	};
	SideWallGable(Base + Tt * HL);
	SideWallGable(Base - Tt * HL);

	// Trap (rechte steek) achterin langs Tt + 2e vloer met trapgat.
	const int32 NSt = 9;
	const float Rise = FloorH / NSt;
	const float Run = 320.f;
	const float StairLaneN = -HD + 60.f;   // tegen de achterwand
	const float RunFit = FMath::Min(Run, L - 40.f);
	const float StepRun = RunFit / NSt;
	auto Slab = [&](const FVector& c, float lenN, float lenTt) { const float sx = bAlongX ? lenN : lenTt, sy = bAlongX ? lenTt : lenN; AddBox(Cube, FVector(c.X, c.Y, TopZ + FloorH), FVector(sx, sy, 12.f), FloorC, true); };
	// 2e vloer = unit minus trapgat (gat: N[-HD..-HD+120] x Tt[-HL..-HL+RunFit]).
	const float HoleN = 120.f;
	Slab(Base + N * ((-HD + HoleN + HD) * 0.5f), (D - HoleN), L);                                   // voorste strook (volle Tt)
	Slab(Base + N * (-HD + HoleN * 0.5f) + Tt * ((-HL + RunFit + HL) * 0.5f), HoleN, (L - RunFit)); // strook naast het gat
	for (int32 s = 0; s < NSt; ++s)
	{
		const float ttPos = -HL + (s + 0.5f) * StepRun;
		const float treadTop = TopZ + (s + 1) * Rise;
		const FVector c = Base + N * StairLaneN + Tt * ttPos;
		const float sx = bAlongX ? 100.f : (StepRun + 4.f), sy = bAlongX ? (StepRun + 4.f) : 100.f;
		AddBox(Cube, FVector(c.X, c.Y, treadTop - 6.f), FVector(sx, sy, 12.f), StepC, true);
	}

	// Werkende voordeur in de opening.
	const FVector Hinge = Base + N * HD - Tt * (DoorW * 0.5f) + FVector(0.f, 0.f, TopZ);
	const float DoorYaw = FMath::RadiansToDegrees(FMath::Atan2(Tt.Y, Tt.X));
	FActorSpawnParameters DSP; DSP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ACityDoor* FrontDoor = W->SpawnActor<ACityDoor>(ACityDoor::StaticClass(), FTransform(FRotator(0.f, DoorYaw, 0.f), Hinge), DSP);
	if (FrontDoor) { FrontDoor->Setup(DoorW - 4.f, DoorH - 4.f, DoorC); }

	// Eén plafondlamp beneden en één boven (iets naar voren, weg van de trap achterin).
	const FVector LampXY = Base + N * (HD * 0.35f);
	AddInteriorLight(FVector(LampXY.X, LampXY.Y, TopZ + FloorH - 40.f)); // begane grond
	AddInteriorLight(FVector(LampXY.X, LampXY.Y, TopZ + WallH - 20.f));  // bovenverdieping
	return FrontDoor;
}

void ACityGenerator::BuildWallWindows(float CenterX, float CenterY, bool bAlongX, float Length, float BaseZ,
	int32 Floors, float FloorH, float T, const FLinearColor& Wall, float DoorCenter, float DoorW, float DoorTop)
{
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!Cube) { return; }

	const FLinearColor Glass(0.34f, 0.50f, 0.62f);
	const FLinearColor Mull = Wall * 0.7f;
	UMaterialInterface* GlassMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/_Project/Materials/M_Glass.M_Glass"));
	const float SillH = 95.f, WinH = 130.f;
	const float HalfL = Length * 0.5f;
	const float A0 = (bAlongX ? CenterX : CenterY) - HalfL; // begin langs de muur-as
	const float Perp = (bAlongX ? CenterY : CenterX);

	// Eén stuk muur: aCenter (langs de as), aLen (lengte), zC (midden hoogte), zH (hoogte). glass=dun + see-through.
	auto Seg = [&](float aCenter, float aLen, float zC, float zH, const FLinearColor& Col, bool bGlass) -> UStaticMeshComponent*
	{
		if (aLen < 2.f || zH < 2.f) { return nullptr; }
		const float Th = bGlass ? 6.f : T;
		const FVector C = bAlongX ? FVector(aCenter, Perp, zC) : FVector(Perp, aCenter, zC);
		const FVector S = bAlongX ? FVector(aLen, Th, zH) : FVector(Th, aLen, zH);
		return AddBox(Cube, C, S, Col, true);
	};
	// Borstwering + glasstrook + latei over een muur-stuk [s0..s1] op verdieping met basis fb.
	auto Bands = [&](float s0, float s1, float fb)
	{
		const float L = s1 - s0; if (L < 12.f) { return; }
		const float c = (s0 + s1) * 0.5f;
		Seg(c, L, fb + SillH * 0.5f, SillH, Wall, false);                          // borstwering onder
		const float headZ0 = fb + SillH + WinH, headH = FloorH - (SillH + WinH);
		Seg(c, L, headZ0 + headH * 0.5f, headH, Wall, false);                      // latei boven
		// Glasstrook: DOORZICHTIG materiaal -> echt see-through raam.
		if (UStaticMeshComponent* G = Seg(c, L, fb + SillH + WinH * 0.5f, WinH, Glass, true))
		{
			if (GlassMat) { G->SetMaterial(0, GlassMat); }
		}
		const int32 NM = FMath::Max(0, (int32)(L / 170.f));
		for (int32 m = 1; m <= NM; ++m) { Seg(s0 + L * m / (NM + 1), 12.f, fb + SillH + WinH * 0.5f, WinH, Mull, false); }
	};

	for (int32 f = 0; f < Floors; ++f)
	{
		const float fb = BaseZ + f * FloorH;
		if (f == 0 && DoorW > 0.f)
		{
			const float dl = DoorCenter - DoorW * 0.5f, dr = DoorCenter + DoorW * 0.5f;
			Bands(A0, dl, fb);
			Bands(dr, A0 + Length, fb);
			const float dH = FloorH - DoorTop;
			Seg(DoorCenter, DoorW, fb + DoorTop + dH * 0.5f, dH, Wall, false); // latei boven de deur
		}
		else
		{
			Bands(A0, A0 + Length, fb);
		}
	}
}

void ACityGenerator::BuildPark(float CX, float CY, float Size, float GroundTopZ)
{
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	UStaticMesh* Cyl = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (!Cube) { return; }

	const float Half = Size * 0.5f;
	const float Z = GroundTopZ;
	const FLinearColor Grass(0.16f, 0.36f, 0.14f);
	const FLinearColor PathC(0.58f, 0.55f, 0.50f);
	const FLinearColor Fence(0.20f, 0.20f, 0.22f);
	const FLinearColor Trunk(0.28f, 0.18f, 0.10f);
	const FLinearColor Leaf1(0.16f, 0.38f, 0.15f), Leaf2(0.20f, 0.44f, 0.18f);
	const FLinearColor Wood(0.40f, 0.28f, 0.16f);

	// Gras + kruispad door het midden.
	AddBox(Cube, FVector(CX, CY, Z + 3.f), FVector(Size, Size, 6.f), Grass, true);
	const float PW = 260.f;
	AddBox(Cube, FVector(CX, CY, Z + 7.f), FVector(Size, PW, 6.f), PathC, false);
	AddBox(Cube, FVector(CX, CY, Z + 7.f), FVector(PW, Size, 6.f), PathC, false);

	// Laag hekje rond de rand, met een opening waar elk pad uitkomt.
	const float FH = 75.f, GapW = PW + 50.f;
	auto FenceSeg = [&](float x0, float y0, float x1, float y1)
	{
		const float lx = FMath::Abs(x1 - x0), ly = FMath::Abs(y1 - y0);
		if (lx + ly < 12.f) { return; }
		AddBox(Cube, FVector((x0 + x1) * 0.5f, (y0 + y1) * 0.5f, Z + FH * 0.5f), FVector(FMath::Max(lx, 10.f), FMath::Max(ly, 10.f), FH), Fence, true);
	};
	FenceSeg(CX - Half, CY + Half, CX - GapW * 0.5f, CY + Half); FenceSeg(CX + GapW * 0.5f, CY + Half, CX + Half, CY + Half);
	FenceSeg(CX - Half, CY - Half, CX - GapW * 0.5f, CY - Half); FenceSeg(CX + GapW * 0.5f, CY - Half, CX + Half, CY - Half);
	FenceSeg(CX + Half, CY - Half, CX + Half, CY - GapW * 0.5f); FenceSeg(CX + Half, CY + GapW * 0.5f, CX + Half, CY + Half);
	FenceSeg(CX - Half, CY - Half, CX - Half, CY - GapW * 0.5f); FenceSeg(CX - Half, CY + GapW * 0.5f, CX - Half, CY + Half);

	// Boompjes in de 4 kwadranten (buiten de paden).
	if (Cyl && Sphere)
	{
		const float Q = Size * 0.27f;
		const float DX[4] = { Q, Q, -Q, -Q }, DY[4] = { Q, -Q, Q, -Q };
		for (int32 i = 0; i < 4; ++i)
		{
			const FVector B(CX + DX[i], CY + DY[i], Z);
			AddBox(Cyl, B + FVector(0, 0, 95), FVector(28, 28, 190), Trunk, true);
			AddBox(Sphere, B + FVector(0, 0, 250), FVector(200, 200, 180), Leaf1, false);
			AddBox(Sphere, B + FVector(45, 25, 320), FVector(150, 150, 140), Leaf2, false);
		}
	}

	// Bankjes langs het kruispad.
	auto Bench = [&](float bx, float by, bool alongX)
	{
		const FLinearColor Legs(0.15f, 0.14f, 0.13f);
		const float Wd = 120.f;
		AddBox(Cube, FVector(bx, by, Z + 45.f), alongX ? FVector(Wd, 42.f, 8.f) : FVector(42.f, Wd, 8.f), Wood, true);
		AddBox(Cube, FVector(bx, by, Z + 70.f) + (alongX ? FVector(0, -17.f, 0) : FVector(-17.f, 0, 0)), alongX ? FVector(Wd, 8.f, 40.f) : FVector(8.f, Wd, 40.f), Wood, true);
		AddBox(Cube, FVector(bx, by, Z + 22.f) + (alongX ? FVector(-Wd * 0.4f, 0, 0) : FVector(0, -Wd * 0.4f, 0)), FVector(8, 38, 44), Legs, false);
		AddBox(Cube, FVector(bx, by, Z + 22.f) + (alongX ? FVector(Wd * 0.4f, 0, 0) : FVector(0, Wd * 0.4f, 0)), FVector(8, 38, 44), Legs, false);
	};
	const float Bo = PW * 0.5f + 95.f;
	Bench(CX, CY + Bo, true);  Bench(CX, CY - Bo, true);
	Bench(CX + Bo, CY, false); Bench(CX - Bo, CY, false);

	// Fatsoenlijke verlichting op elke hoek van het park (gaat 's avonds aan, regelbaar via de slider).
	const float Lc = Half - 75.f;
	AddCityLamp(FVector(CX - Lc, CY + Lc, Z));
	AddCityLamp(FVector(CX + Lc, CY + Lc, Z));
	AddCityLamp(FVector(CX - Lc, CY - Lc, Z));
	AddCityLamp(FVector(CX + Lc, CY - Lc, Z));
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

		// ATM naast de balie in elke winkel (gericht naar de deur, zodat je er vóór kunt staan).
		{
			const FVector Tang(-N.Y, N.X, 0.f);
			FVector AtmPos = CounterPos + Tang * 210.f;
			AtmPos.Z = BaseZ + 2.f;
			W->SpawnActor<AAtm>(AAtm::StaticClass(), FTransform(FRotator(0.f, Yaw, 0.f), AtmPos), SP);
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
