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
#include "Engine/PostProcessVolume.h"
#include "Engine/Scene.h"
#include "World/CeilingLamp.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"

TWeakObjectPtr<ADayNightController> ADayNightController::LocalInstance;

ADayNightController::ADayNightController()
{
	PrimaryActorTick.bCanEverTick = true;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
}

ADayNightController* ADayNightController::GetLocal(UWorld* W)
{
	if (LocalInstance.IsValid()) { return LocalInstance.Get(); }
	if (W) { for (TActorIterator<ADayNightController> It(W); It; ++It) { LocalInstance = *It; return *It; } }
	return nullptr;
}

void ADayNightController::SaveLightConfig() const
{
	const FString Cfg = FString::Printf(
		TEXT("MoonIntensity=%.3f\nSunIntensity=%.3f\nSkyNight=%.3f\nSkyDay=%.3f\nMoonPitch=%.1f\nLampIntensity=%.0f\nExposureBias=%.2f\n"),
		MoonIntensity, SunIntensity, SkyNight, SkyDay, MoonPitch, LampIntensity, ExposureBias);
	const FString Path = FPaths::ProjectSavedDir() / TEXT("LightConfig.txt");
	FFileHelper::SaveStringToFile(Cfg, *Path);
	UE_LOG(LogTemp, Warning, TEXT("[LightConfig] saved to %s\n%s"), *Path, *Cfg);
}

const UDayCycleComponent* ADayNightController::GetDayCycle() const
{
	const AWeedShopGameState* GS = GetWorld() ? GetWorld()->GetGameState<AWeedShopGameState>() : nullptr;
	return GS ? GS->GetDayCycle() : nullptr;
}

void ADayNightController::TryAdoptSky()
{
	UWorld* W = GetWorld();
	if (!W || Sky.IsValid()) { return; }
	for (TActorIterator<ASkyLight> It(W); It; ++It) { Sky = *It; break; }
	if (Sky.IsValid() && Sky->GetLightComponent())
	{
		USkyLightComponent* SkyComp = Sky->GetLightComponent();
		SkyComp->SetMobility(EComponentMobility::Movable);
		// BELANGRIJK: een Movable SkyLight zonder verse capture heeft een lege/oude cubemap.
		// Realtime capture houdt de cubemap geldig -> ambient dimt netjes mee met de nacht.
		SkyComp->SetRealTimeCaptureEnabled(true);
		SkyComp->RecaptureSky();
	}
}

void ADayNightController::BeginPlay()
{
	Super::BeginPlay();

	UWorld* W = GetWorld();
	if (!W) { return; }

	LocalInstance = this; // bereikbaar voor de phone-UI (live tunen)

	// Bestaande zon (directional light) zoeken; anders zelf een Movable aanmaken.
	for (TActorIterator<ADirectionalLight> It(W); It; ++It) { Sun = *It; break; }
	if (!Sun.IsValid())
	{
		Sun = W->SpawnActor<ADirectionalLight>(ADirectionalLight::StaticClass(), FTransform(FRotator(-45.f, 35.f, 0.f)));
	}
	if (Sun.IsValid() && Sun->GetLightComponent())
	{
		Sun->GetLightComponent()->SetMobility(EComponentMobility::Movable); // runtime draaibaar
		Sun->GetLightComponent()->SetCastShadows(true); // dak houdt de zon tegen -> niet binnen
		// ONZE zon is de atmosfeer-zon: de zon-schijf/hemel volgt onze rotatie (pack-maps hebben
		// eigen zonnen in gestreamde lighting-scenario's - die schakelt de DoorRetrofitter uit).
		if (UDirectionalLightComponent* SunDL = Cast<UDirectionalLightComponent>(Sun->GetLightComponent()))
		{
			SunDL->SetAtmosphereSunLight(true);
			// Via de SETTERS (direct de property zetten ververst de render-state niet -> bleef enorm).
			SunDL->SetLightSourceAngle(1.5f);      // zelfde formaat als de maan (lekker zichtbaar)
			SunDL->SetLightSourceSoftAngle(0.f);   // geen extra zachte rand
		}
	}

	// MAAN: eigen tweede directional light in tegenfase met de zon (komt op bij zonsondergang,
	// gaat onder bij zonsopkomst) met echte maan-schijf via atmosphere-light-index 1.
	if (!Moon.IsValid())
	{
		Moon = W->SpawnActor<ADirectionalLight>(ADirectionalLight::StaticClass(), FTransform(FRotator(-45.f, 157.5f, 0.f)));
		if (Moon.IsValid() && Moon->GetLightComponent())
		{
			Moon->GetLightComponent()->SetMobility(EComponentMobility::Movable);
			Moon->GetLightComponent()->SetCastShadows(true);
			if (UDirectionalLightComponent* MoonDL = Cast<UDirectionalLightComponent>(Moon->GetLightComponent()))
			{
				MoonDL->SetAtmosphereSunLight(true);
				MoonDL->SetAtmosphereSunLightIndex(1); // index 1 = tweede hemel-lichaam (maan-schijf)
				MoonDL->SetLightSourceAngle(1.5f);     // duidelijke maan, maar geen mega-bal
				MoonDL->SetLightSourceSoftAngle(0.f);
				MoonDL->SetLightColor(FLinearColor(0.62f, 0.68f, 0.85f)); // koel maanlicht
			}
		}
	}

	// SkyLight voor ambient (zoek bestaande; in pack-maps streamt 'ie later in -> TryAdoptSky).
	TryAdoptSky();

	// Vaste belichting (geen auto-exposure): anders wordt buiten donker zodra je in een lichte kamer
	// kijkt en omgekeerd. Een onbegrensd Post Process Volume met Manual exposure houdt alles gelijk.
	{
		APostProcessVolume* PP = W->SpawnActor<APostProcessVolume>(APostProcessVolume::StaticClass(), FTransform::Identity);
		if (PP)
		{
			PP->bUnbound = true;
			PP->BlendWeight = 1.f;
			PP->Priority = 1000.f;
			FPostProcessSettings& S = PP->Settings;
			S.bOverride_AutoExposureMethod = true;
			S.AutoExposureMethod = EAutoExposureMethod::AEM_Manual;
			S.bOverride_AutoExposureBias = true;
			S.AutoExposureBias = ExposureBias; // vaste belichtingscompensatie (lager = donkerder)
			S.bOverride_BloomIntensity = true;
			S.BloomIntensity = 0.35f; // bescheiden bloom: geen mega-gloed rond de zon
			PPV = PP;
		}
	}

	// (Oude per-NPC lantaarnpalen verwijderd: de stad-straatlampen van de CityGenerator zijn nu de
	// straatverlichting, en die worden door de phone-slider (LampIntensity) live geregeld.)

	// Alleen in onze eigen (gegenereerde) stad: pack-maps hebben hun eigen interieur-verlichting.
	const FString MapPath = W->GetOutermost()->GetName();
	if (!MapPath.StartsWith(TEXT("/Game/CityBeachStrip")))
	{
		ReplaceIndoorLights();
	}
}

void ADayNightController::ReplaceIndoorLights()
{
	UWorld* W = GetWorld();
	if (!W) { return; }

	// Verzamel bestaande binnen-lampen (point/spot, niet van ons) en zet ze uit.
	TArray<FVector> Spots;
	auto Consider = [&](ULightComponent* L)
	{
		if (!IsValid(L) || L->GetWorld() != W || L->GetOwner() == this) { return; }
		const FVector Pos = L->GetComponentLocation();
		FHitResult Up;
		FCollisionQueryParams Q(FName(TEXT("IndoorLight")), false, this);
		const bool bIndoors = W->LineTraceSingleByChannel(Up, Pos, Pos + FVector(0.f, 0.f, 1300.f), ECC_WorldStatic, Q);
		if (bIndoors)
		{
			L->SetVisibility(false); // felle map-lamp uit
			Spots.Add(Pos);
		}
	};
	for (TObjectIterator<UPointLightComponent> It; It; ++It) { Consider(*It); }
	for (TObjectIterator<USpotLightComponent> It; It; ++It) { Consider(*It); }

	// Vervang door het echte plafondlamp-model (warme spot, oppakbaar). Alleen de server spawnt
	// de gerepliceerde actor; clients krijgen 'm via replicatie.
	if (W->GetNetMode() != NM_Client)
	{
		for (const FVector& P : Spots)
		{
			FActorSpawnParameters SP;
			SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			W->SpawnActor<ACeilingLamp>(ACeilingLamp::StaticClass(), FTransform(P), SP);
		}
	}
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
	UStaticMesh* Cone = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cone.Cone"));
	UMaterialInterface* BaseMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));

	const float PoleH = 420.f; // cm
	const FLinearColor Metal(0.06f, 0.07f, 0.09f); // donker staal

	auto AddMesh = [&](UStaticMesh* M, const FVector& Loc, const FVector& Scale, const FLinearColor& Col, const FRotator& Rot) -> UStaticMeshComponent*
	{
		if (!M) { return nullptr; }
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
		C->SetupAttachment(Root);
		C->RegisterComponent();
		C->SetStaticMesh(M);
		C->SetWorldLocation(Loc);
		C->SetWorldRotation(Rot);
		C->SetWorldScale3D(Scale);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		if (BaseMat) { if (UMaterialInstanceDynamic* MID = C->CreateDynamicMaterialInstance(0, BaseMat)) { MID->SetVectorParameterValue(TEXT("Color"), Col); } }
		return C;
	};

	// Voetstuk (breed, laag) onderaan de paal.
	AddMesh(Cyl, BaseOnGround + FVector(0.f, 0.f, 7.f), FVector(0.35f, 0.35f, 0.14f), Metal, FRotator::ZeroRotator);
	// Paal (donker, dun).
	AddMesh(Cyl, BaseOnGround + FVector(0.f, 0.f, PoleH * 0.5f), FVector(0.1f, 0.1f, PoleH / 100.f), Metal, FRotator::ZeroRotator);
	// Lantaarn-kapje (kegel, wijde kant omlaag) bovenaan.
	AddMesh(Cone, BaseOnGround + FVector(0.f, 0.f, PoleH + 18.f), FVector(0.5f, 0.5f, 0.4f), Metal, FRotator(180.f, 0.f, 0.f));
	// Klein dakje op de kegelpunt.
	AddMesh(Sphere, BaseOnGround + FVector(0.f, 0.f, PoleH + 40.f), FVector(0.12f, 0.12f, 0.1f), Metal, FRotator::ZeroRotator);

	// Lampbol in het kapje (kleurt warmgeel als 'ie aan is). Dit is de "kop" die we togglen.
	if (Sphere)
	{
		UStaticMeshComponent* Head = NewObject<UStaticMeshComponent>(this);
		Head->SetupAttachment(Root);
		Head->RegisterComponent();
		Head->SetStaticMesh(Sphere);
		Head->SetWorldLocation(BaseOnGround + FVector(0.f, 0.f, PoleH + 4.f));
		Head->SetWorldScale3D(FVector(0.22f, 0.22f, 0.22f));
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
		PL->RegisterComponent();
		PL->SetWorldLocation(BaseOnGround + FVector(0.f, 0.f, PoleH - 10.f));
		PL->SetMobility(EComponentMobility::Movable);
		PL->SetAttenuationRadius(1600.f);
		PL->SetLightColor(FLinearColor(1.f, 0.82f, 0.5f));
		PL->SetIntensity(0.f); // physically-based (lumens) -> consistent met de vaste belichting
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

	// ZON: doorlopende 24-uurs boog zoals in het echt - komt op rond ZZO, piekt 's middags en zakt
	// 's avonds echt onder de horizon tussen NW en N (yaw 337.5). 's Nachts staat 'ie onder de horizon.
	if (Sun.IsValid() && Sun->GetLightComponent())
	{
		// Boog gekoppeld aan zonsopkomst/-ondergang uit de klok: pitch 0 bij opkomst, -90 rond de
		// middag, 0 bij ondergang, en 's nachts door naar +(onder de horizon).
		const float DayLen = FMath::Max(1.f, Sunset - Sunrise);
		const float SunPhase = (Hour - Sunrise) / DayLen;      // 0 = opkomst, 1 = ondergang
		const float SunPitch = -180.f * SunPhase;               // -180-boog over de dag, daarna onder de horizon
		Sun->SetActorRotation(FRotator(SunPitch, 337.5f, 0.f));

		UDirectionalLightComponent* DL = Cast<UDirectionalLightComponent>(Sun->GetLightComponent());
		if (DL)
		{
			DL->SetIntensity(SunIntensity * FMath::Max(DayF, 0.02f)); // onder de horizon vrijwel uit
			const FLinearColor Warm(1.f, 0.96f, 0.88f);
			const FLinearColor Dawn(1.f, 0.72f, 0.5f); // gouden randje rond op-/ondergang
			DL->SetLightColor(FMath::Lerp(Dawn, Warm, FMath::Clamp(DayF * 1.4f - 0.2f, 0.f, 1.f)));
		}
	}

	// MAAN: tegenfase - komt op bij zonsondergang, staat rond 1u 's nachts hoog en gaat onder bij
	// zonsopkomst. Levert het navigeerbare nachtlicht (MoonIntensity) en een echte maan-schijf.
	if (Moon.IsValid() && Moon->GetLightComponent())
	{
		const float NightLen = FMath::Max(1.f, 24.f - (Sunset - Sunrise));
		float NightHour = Hour - Sunset;                        // uren sinds zonsondergang
		if (NightHour < 0.f) { NightHour += 24.f; }
		const float MoonPhase = NightHour / NightLen;           // 0 = maan-opkomst, 1 = maan-ondergang
		const float MoonPitchNow = -180.f * MoonPhase;
		Moon->SetActorRotation(FRotator(MoonPitchNow, 322.5f, 0.f)); // ondergang net iets westelijker dan de zon

		if (UDirectionalLightComponent* MoonDL = Cast<UDirectionalLightComponent>(Moon->GetLightComponent()))
		{
			MoonDL->SetIntensity(MoonIntensity * (1.f - DayF)); // overdag uit, 's nachts vol
		}
	}

	// SkyLight-ambient: 's nachts iets opgekrikt zodat tussen de lampen niet alles wegvalt in zwart.
	if (Sky.IsValid() && Sky->GetLightComponent())
	{
		Sky->GetLightComponent()->SetIntensity(FMath::Lerp(SkyNight, SkyDay, DayF));
	}

	// Live exposure-tuning.
	if (PPV.IsValid())
	{
		PPV->Settings.AutoExposureBias = ExposureBias;
	}

	// Straatlampen op kloktijd: 's avonds aan (vanaf 17u), 's ochtends uit rond 8u.
	const int32 WantOn = (Hour < 8.f || Hour >= 17.f) ? 1 : 0;
	const bool bStateChanged = (WantOn != bLampsOn);
	const bool bIntChanged = (WantOn == 1 && !FMath::IsNearlyEqual(LampIntensity, LastLampApplied, 1.f));
	if (bStateChanged || bIntChanged)
	{
		bLampsOn = WantOn;
		const float I = (WantOn == 1) ? LampIntensity : 0.f;
		for (UPointLightComponent* PL : LampLights) { if (PL) { PL->SetIntensity(I); } }
		LastLampApplied = LampIntensity;
		if (bStateChanged)
		{
			for (UMaterialInstanceDynamic* M : LampHeadMats)
			{
				if (M) { M->SetVectorParameterValue(TEXT("Color"), WantOn ? FLinearColor(1.f, 0.85f, 0.45f) : FLinearColor(0.2f, 0.2f, 0.22f)); }
			}
		}
	}
}
