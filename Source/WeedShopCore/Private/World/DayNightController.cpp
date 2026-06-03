#include "World/DayNightController.h"

#include "World/DayCycleComponent.h"
#include "Game/WeedShopGameState.h"

#include "Engine/DirectionalLight.h"
#include "Components/DirectionalLightComponent.h"
#include "Engine/SkyLight.h"
#include "Components/SkyLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "UObject/UObjectIterator.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "GameFramework/PlayerStart.h"
#include "Customer/CustomerBase.h"
#include "Engine/World.h"
#include "EngineUtils.h"

ADayNightController::ADayNightController()
{
	PrimaryActorTick.bCanEverTick = true;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
}

const UDayCycleComponent* ADayNightController::GetDayCycle() const
{
	const AWeedShopGameState* GS = GetWorld() ? GetWorld()->GetGameState<AWeedShopGameState>() : nullptr;
	return GS ? GS->GetDayCycle() : nullptr;
}

void ADayNightController::BeginPlay()
{
	Super::BeginPlay();

	UWorld* W = GetWorld();
	if (!W) { return; }

	// Bestaande zon (directional light) zoeken; anders zelf een Movable aanmaken.
	for (TActorIterator<ADirectionalLight> It(W); It; ++It) { Sun = *It; break; }
	if (!Sun.IsValid())
	{
		Sun = W->SpawnActor<ADirectionalLight>(ADirectionalLight::StaticClass(), FTransform(FRotator(-45.f, 35.f, 0.f)));
	}
	if (Sun.IsValid() && Sun->GetLightComponent())
	{
		Sun->GetLightComponent()->SetMobility(EComponentMobility::Movable); // runtime draaibaar
	}

	// SkyLight voor ambient (zoek bestaande).
	for (TActorIterator<ASkyLight> It(W); It; ++It) { Sky = *It; break; }
	if (Sky.IsValid() && Sky->GetLightComponent())
	{
		Sky->GetLightComponent()->SetMobility(EComponentMobility::Movable);
	}

	// Lantaarnpalen rond een referentiepunt (PlayerStart, anders deze actor).
	FVector Center = GetActorLocation();
	for (TActorIterator<APlayerStart> It(W); It; ++It) { Center = It->GetActorLocation(); break; }
	BuildStreetLamps(Center);
}

void ADayNightController::BuildStreetLamps(const FVector& Center)
{
	UWorld* W = GetWorld();
	if (!W) { return; }

	// Eén lantaarn naast ELKE NPC die BUITEN staat (NPC's binnen het huis krijgen er geen).
	// Geen ring, geen skip-checks: precies één paal per buiten-NPC, op de stoep (naar de muur gesnapt).
	for (TActorIterator<ACustomerBase> It(W); It; ++It)
	{
		ACustomerBase* Npc = *It;
		if (!IsValid(Npc)) { continue; }
		const FVector Loc = Npc->GetActorLocation();
		const FVector Eye = Loc + FVector(0.f, 0.f, 90.f);

		// Plafond boven de NPC = binnen -> overslaan.
		FHitResult Ceiling;
		FCollisionQueryParams Qc(FName(TEXT("LampCeiling")), false, this);
		if (W->LineTraceSingleByChannel(Ceiling, Eye, Loc + FVector(0.f, 0.f, 1300.f), ECC_WorldStatic, Qc)) { continue; }

		// Dichtstbijzijnde muur zoeken (8 richtingen) zodat de paal op de stoep tegen het gebouw komt.
		bool bFound = false; float BestDist = 1e9f; FVector BestPoint; FVector BestDir;
		for (int32 d = 0; d < 8; ++d)
		{
			const float A = (2.f * PI * d) / 8.f;
			const FVector Dir(FMath::Cos(A), FMath::Sin(A), 0.f);
			FHitResult Wall;
			FCollisionQueryParams Qw(FName(TEXT("LampWall")), false, this);
			Qw.AddIgnoredActor(Npc);
			if (W->LineTraceSingleByChannel(Wall, Eye, Eye + Dir * 1800.f, ECC_WorldStatic, Qw))
			{
				const float Dist = FVector::Dist(Eye, Wall.ImpactPoint);
				if (Dist > 120.f && Dist < BestDist) { BestDist = Dist; BestPoint = Wall.ImpactPoint; BestDir = Dir; bFound = true; }
			}
		}

		// Plek: ~110cm vóór de muur (op de stoep); anders gewoon vlak naast de NPC.
		const FVector LampXY = bFound ? (BestPoint - BestDir * 110.f) : (Loc + FVector(160.f, 120.f, 0.f));

		// Grond zoeken vanaf NPC-hoogte omlaag (niet van hoog, anders vang je het dak/de luifel).
		FHitResult Ground;
		FCollisionQueryParams Qg(FName(TEXT("LampNpcGround")), false, this);
		Qg.AddIgnoredActor(Npc);
		if (W->LineTraceSingleByChannel(Ground, FVector(LampXY.X, LampXY.Y, Loc.Z + 150.f), FVector(LampXY.X, LampXY.Y, Loc.Z - 600.f), ECC_WorldStatic, Qg))
		{
			AddLamp(Ground.Location);
		}
		else
		{
			AddLamp(Loc - FVector(0.f, 0.f, 88.f)); // altijd één lamp, ook zonder vloer-hit
		}
	}
}

void ADayNightController::AddLamp(const FVector& BaseOnGround)
{
	UStaticMesh* Cyl = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	UMaterialInterface* BaseMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));

	const float PoleH = 420.f; // cm

	// Paal (donker, dun).
	if (Cyl)
	{
		UStaticMeshComponent* Pole = NewObject<UStaticMeshComponent>(this);
		Pole->SetupAttachment(Root);
		Pole->RegisterComponent();
		Pole->SetStaticMesh(Cyl);
		Pole->SetWorldLocation(BaseOnGround + FVector(0.f, 0.f, PoleH * 0.5f));
		Pole->SetWorldScale3D(FVector(0.12f, 0.12f, PoleH / 100.f)); // cylinder = 100cm
		Pole->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		if (BaseMat)
		{
			UMaterialInstanceDynamic* M = Pole->CreateDynamicMaterialInstance(0, BaseMat);
			if (M) { M->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.06f, 0.07f, 0.09f)); }
		}
	}

	// Lampkop (bol bovenaan, kleurt geel als 'ie aan is).
	if (Sphere)
	{
		UStaticMeshComponent* Head = NewObject<UStaticMeshComponent>(this);
		Head->SetupAttachment(Root);
		Head->RegisterComponent();
		Head->SetStaticMesh(Sphere);
		Head->SetWorldLocation(BaseOnGround + FVector(0.f, 0.f, PoleH + 8.f));
		Head->SetWorldScale3D(FVector(0.28f, 0.28f, 0.28f));
		Head->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		if (BaseMat)
		{
			UMaterialInstanceDynamic* M = Head->CreateDynamicMaterialInstance(0, BaseMat);
			if (M) { M->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.2f, 0.2f, 0.22f)); }
			LampHeadMats.Add(M);
		}
		LampHeads.Add(Head);
	}

	// Warm puntlicht net onder de kop (begint uit). Inverse-squared UIT -> gelijkmatige poel zonder
	// uitgeblazen plekken vlakbij; lage intensiteit volstaat dan.
	{
		UPointLightComponent* PL = NewObject<UPointLightComponent>(this);
		PL->SetupAttachment(Root);
		PL->bUseInverseSquaredFalloff = false;
		PL->RegisterComponent();
		PL->SetWorldLocation(BaseOnGround + FVector(0.f, 0.f, PoleH - 10.f));
		PL->SetMobility(EComponentMobility::Movable);
		PL->SetAttenuationRadius(1850.f);
		PL->SetLightColor(FLinearColor(1.f, 0.82f, 0.5f));
		PL->SetIntensity(0.f);
		PL->SetCastShadows(false);
		LampLights.Add(PL);
	}
}

void ADayNightController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	const UDayCycleComponent* DC = GetDayCycle();
	if (!DC) { return; }

	const float Hour = DC->GetClockHour();         // 0..24
	const float Sunrise = DC->SunriseHour;          // 6
	const float Sunset = DC->SunsetHour;            // 20

	// Daglicht-factor (0 = nacht, 1 = klaarlichte dag) met zachte overgangen rond op-/ondergang.
	float DayF;
	if (Hour <= Sunrise - 1.f || Hour >= Sunset + 1.f)      { DayF = 0.f; }
	else if (Hour < Sunrise + 1.f)                          { DayF = (Hour - (Sunrise - 1.f)) * 0.5f; } // 2u dageraad
	else if (Hour > Sunset - 1.f)                           { DayF = 1.f - (Hour - (Sunset - 1.f)) * 0.5f; } // 2u schemering
	else                                                    { DayF = 1.f; }
	DayF = FMath::Clamp(DayF, 0.f, 1.f);

	// Zon: roteren met de klok (overhead om 12u, onder de horizon 's nachts) + dimmen/kleuren.
	if (Sun.IsValid() && Sun->GetLightComponent())
	{
		const float Pitch = 90.f - Hour * 15.f; // 6u=0 (horizon), 12u=-90 (overhead), 0u=+90 (onder)
		Sun->SetActorRotation(FRotator(Pitch, 35.f, 0.f));

		UDirectionalLightComponent* DL = Cast<UDirectionalLightComponent>(Sun->GetLightComponent());
		if (DL)
		{
			// Nacht-ondergrens wat hoger zodat het buiten niet pikzwart is (maanlicht).
			DL->SetIntensity(FMath::Lerp(0.25f, 7.f, DayF));
			const FLinearColor Warm(1.f, 0.96f, 0.88f);
			const FLinearColor NightBlue(0.4f, 0.48f, 0.78f);
			DL->SetLightColor(FMath::Lerp(NightBlue, Warm, DayF));
		}
	}

	// SkyLight ambient mee dimmen (nacht-vloer hoger, anders te donker buiten).
	if (Sky.IsValid() && Sky->GetLightComponent())
	{
		Sky->GetLightComponent()->SetIntensity(FMath::Lerp(0.55f, 1.1f, DayF));
	}

	// Lantaarnpalen aan zodra het echt begint te schemeren (eerder dan voorheen).
	const int32 WantOn = (DayF < 0.55f) ? 1 : 0;
	if (WantOn != bLampsOn)
	{
		bLampsOn = WantOn;
		for (UPointLightComponent* PL : LampLights) { if (PL) { PL->SetIntensity(WantOn ? 17.f : 0.f); } }
		for (UMaterialInstanceDynamic* M : LampHeadMats)
		{
			if (M) { M->SetVectorParameterValue(TEXT("Color"), WantOn ? FLinearColor(1.f, 0.85f, 0.45f) : FLinearColor(0.2f, 0.2f, 0.22f)); }
		}
	}
}
