#include "World/DayNightController.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/LevelStreaming.h"
#include "Components/SkyAtmosphereComponent.h"

#include "World/DayCycleComponent.h"
#include "Game/WeedShopGameState.h"

#include "Engine/DirectionalLight.h"
#include "Components/DirectionalLightComponent.h"
#include "Engine/SkyLight.h"
#include "Components/SkyLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/LocalLightComponent.h"
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

void ADayNightController::ApplyMapPhotoLight()
{
	// Vaste ochtendstand (zon ~50 graden, schuin uit het ZO-kwadrant van de boog): zelfde look
	// als de captures die er goed uitzagen. Tick draait elk frame en zet alles direct weer terug.
	if (PackSun.IsValid() && PackSun->GetLightComponent())
	{
		PackSun->SetActorRotation(FRotator(-50.f, 337.5f, 0.f));
		PackSun->GetLightComponent()->SetIntensity(SunIntensity);
	}
	if (PackMoon.IsValid() && PackMoon->GetLightComponent())
	{
		PackMoon->GetLightComponent()->SetIntensity(0.f);
	}
	if (NightPPV.IsValid()) { NightPPV->BlendWeight = 0.f; }
	if (BloomPPV.IsValid()) { BloomPPV->BlendWeight = 1.f; }
	for (FDimLight& D : DimLights)
	{
		if (ULightComponent* LC = D.Light.Get())
		{
			if (LC->IsA(USkyLightComponent::StaticClass())) { LC->SetIntensity(D.OrigIntensity); }
		}
	}
	for (UPointLightComponent* PL : PackLampLights)
	{
		if (PL) { PL->SetIntensity(0.f); }
	}
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
		TEXT("MoonIntensity=%.3f\nSunIntensity=%.3f\nSkyNight=%.3f\nSkyDay=%.3f\nMoonPitch=%.1f\nLampIntensity=%.0f\nExposureBias=%.2f\nNightGain=%.3f\nNightExposure=%.2f\nDayBloom=%.3f\nSunHaze=%.5f\n"),
		MoonIntensity, SunIntensity, SkyNight, SkyDay, MoonPitch, LampIntensity, ExposureBias, NightGain, NightExposure, DayBloom, SunHaze);
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

	LoadLightConfig(); // opgeslagen slider-waardes terugladen

	// PACK-MAPS: MINIMAL-modus - overdag blijft de stock-look 100% intact (geen zon/fog/scenario-
	// ingrepen; dat sloopte de art-stijl). 's Nachts dimmen we alleen de BESTAANDE lichten van de
	// map en gaat de HDRI-fotokoepel (dag-lucht) uit zodat de donkere hemel zichtbaar wordt.
	if (W->GetOutermost()->GetName().StartsWith(TEXT("/Game/CityBeachStrip")))
	{
		bPackMinimal = true;
		return; // geen eigen zon/maan/PPV/lampen - Tick regelt alleen het dimmen
	}

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
			SunDL->ForwardShadingPriority = 10;    // zon wint van de maan -> geen 'multiple directional lights'-warning
			SunDL->MarkRenderStateDirty();
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
				MoonDL->ForwardShadingPriority = 0;
				MoonDL->MarkRenderStateDirty();
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
			S.BloomIntensity = 0.5f; // wat sfeer-gloed, maar geen mega-bal
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

	// EÃ©n lantaarn naast ELKE NPC die BUITEN staat (NPC's binnen het huis krijgen er geen).
	// Geen ring, geen skip-checks: precies Ã©Ã©n paal per buiten-NPC, op de stoep (naar de muur gesnapt).
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

		// Plek: ~110cm vÃ³Ã³r de muur (op de stoep); anders gewoon vlak naast de NPC.
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
			AddLamp(Loc - FVector(0.f, 0.f, 88.f)); // altijd Ã©Ã©n lamp, ook zonder vloer-hit
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

	// MINIMAL-modus (pack-maps): dim de bestaande lichten met de klok en toggle de fotokoepel.
	if (bPackMinimal)
	{
		float MinDayF;
		if (Hour <= Sunrise - 1.f || Hour >= Sunset + 1.f)      { MinDayF = 0.f; }
		else if (Hour < Sunrise + 1.f)                          { MinDayF = (Hour - (Sunrise - 1.f)) * 0.5f; }
		else if (Hour > Sunset - 1.f)                           { MinDayF = 1.f - (Hour - (Sunset - 1.f)) * 0.5f; }
		else                                                    { MinDayF = 1.f; }
		MinDayF = FMath::Clamp(MinDayF, 0.f, 1.f);

		// Periodiek nieuwe lichten/koepels registreren (world partition streamt bij).
		LightScanTimer -= DeltaSeconds;
		if (LightScanTimer <= 0.f)
		{
			LightScanTimer = 3.f;
			UWorld* W2 = GetWorld();
			for (TActorIterator<AActor> It(W2); It; ++It)
			{
				AActor* A = *It;
				if (!IsValid(A)) { continue; }
				if (A == PackMoon.Get() || A == PackSun.Get() || A == this) { continue; } // eigen zon/maan/lampen niet mee-dimmen
				TInlineComponentArray<ULightComponent*> Lights(A);
				for (ULightComponent* LC : Lights)
				{
					if (!LC || SeenLights.Contains(LC)) { continue; }
					SeenLights.Add(LC);
					// Map-lampen staan op Static mobility = runtime letterlijk GEEN licht. Movable
					// maken (schaduwen uit voor performance, honderden lampjes) - dit is de oude
					// binnen-lampen fix van voor de minimal-reset; toen deden de lampen het wel.
					if (ULocalLightComponent* LLC = Cast<ULocalLightComponent>(LC))
					{
						if (LLC->Mobility != EComponentMobility::Movable)
						{
							LLC->SetMobility(EComponentMobility::Movable);
							LLC->SetCastShadows(false);
						}
					}
					// Map-zonnen (scenario) mogen de hemel niet sturen: atmosfeer-claim eraf zodat
					// ONZE bewegende zon vanaf seconde een de lucht bepaalt (anders bleef de vaste
					// scenario-zon aan de horizon hangen tot een dag/nacht-wissel).
					if (UDirectionalLightComponent* MapDL = Cast<UDirectionalLightComponent>(LC))
					{
						MapDL->SetMobility(EComponentMobility::Movable);
						MapDL->SetAtmosphereSunLight(false);
						MapDL->MarkRenderStateDirty();
					}
					FDimLight D; D.Light = LC; D.OrigIntensity = LC->Intensity;
					DimLights.Add(D);
				}
				TInlineComponentArray<UStaticMeshComponent*> Meshes(A);
				for (UStaticMeshComponent* MC : Meshes)
				{
					if (!MC || !MC->GetStaticMesh()) { continue; }
					const FString MN = MC->GetStaticMesh()->GetName();
					if (MN.Contains(TEXT("EnviroDome")) || MN.Contains(TEXT("HDRI")))
					{
						DomeComps.AddUnique(MC);
					}
					// PACK-LAMPEN: lantaarns/plafondlampen van de map zijn alleen mesh (geen licht).
					// Warm puntlicht eraan hangen; de klok zet ze 's avonds aan en 's ochtends uit.
					const bool bTallLamp = MN == TEXT("SM_StreetLight_01") || MN == TEXT("SM_StreetLight_02")
						|| MN == TEXT("SM_LampPostBeach_01") || MN == TEXT("SM_AlleyLamp") || MN == TEXT("SM_UtilityPole_Lamp");
					const bool bSmallLamp = MN == TEXT("SM_CeilingLight") || MN == TEXT("SM_CeilingLight02")
						|| MN == TEXT("SM_WallLight01") || MN == TEXT("SM_PoolLight");
					if ((bTallLamp || bSmallLamp) && !PackLampSeen.Contains(MC))
					{
						PackLampSeen.Add(MC);
						const FVector BO = MC->Bounds.Origin;
						UPointLightComponent* PL = nullptr;
						if (bTallLamp)
						{
							// Lantaarn: SPOT met de kegel recht omlaag = realistische lichtpoel op de grond.
							USpotLightComponent* SL2 = NewObject<USpotLightComponent>(this);
							SL2->SetupAttachment(GetRootComponent());
							SL2->RegisterComponent();
							SL2->SetWorldLocationAndRotation(BO + FVector(0.f, 0.f, MC->Bounds.BoxExtent.Z * 0.7f - 50.f), FRotator(-90.f, 0.f, 0.f));
							SL2->SetInnerConeAngle(35.f);
							SL2->SetOuterConeAngle(62.f);
							SL2->SetAttenuationRadius(1500.f);
							PL = SL2;
						}
						else
						{
							UPointLightComponent* PP2 = NewObject<UPointLightComponent>(this);
							PP2->SetupAttachment(GetRootComponent());
							PP2->RegisterComponent();
							PP2->SetWorldLocation(BO + FVector(0.f, 0.f, -30.f));
							PP2->SetAttenuationRadius(850.f);
							PL = PP2;
						}
						PL->SetMobility(EComponentMobility::Movable);
						// Inverse-squared UIT: gelijkmatige poel en de lamp-kop wordt niet weggeblazen
						// (vlakbij de bron explodeerde het kwadratische licht op de kop-mesh).
						PL->bUseInverseSquaredFalloff = false;
						PL->LightFalloffExponent = 2.5f;
						PL->SetLightColor(FLinearColor(1.f, 0.85f, 0.6f));
						PL->SetIntensity(0.f);
						PL->SetCastShadows(false);
						PL->MarkRenderStateDirty();
						PL->ComponentTags.Add(bTallLamp ? TEXT("TallLamp") : TEXT("SmallLamp"));
						PackLampLights.Add(PL);
					}
				}
			}
		}

		// Lichten dimmen. ZONLICHT (directional) moet 's nachts ECHT uit - een restje van 7% werd
		// door de auto-exposure weer opgeblazen tot daglicht onder een zwarte hemel. Skylight bijna
		// uit; alle overige lichten 7% (de emissive strips van de map blijven vanzelf - materiaal).
		const float SunMul = 0.f;                           // map-zonnen permanent uit: onze bewegende zon is DE zon
		const float SkyMul = FMath::Lerp(0.02f, 1.f, MinDayF);
		for (FDimLight& D : DimLights)
		{
			if (ULightComponent* LC = D.Light.Get())
			{
				// Map-lampen (point/spot) volgen de Street lamps slider als schaal (default 42000
				// = 1.0x) - op volle authored sterkte waren ze vervelend fel.
				float LMul = FMath::Clamp(LampIntensity / 42000.f, 0.f, 2.f);
				if (LC->IsA(UDirectionalLightComponent::StaticClass())) { LMul = SunMul; }
				else if (LC->IsA(USkyLightComponent::StaticClass()))    { LMul = SkyMul; }
				const float Want = D.OrigIntensity * LMul;
				if (!FMath::IsNearlyEqual(LC->Intensity, Want, D.OrigIntensity * 0.01f + 0.1f)) { LC->SetIntensity(Want); }
			}
		}
		// Nacht-PPV: overdag gewicht 0 (stock dag-look 100% intact), 's nachts exposure omlaag
		// zodat de auto-exposure de duisternis niet terugcompenseert naar daglicht.
		if (!NightPPV.IsValid())
		{
			if (APostProcessVolume* NightVol = GetWorld()->SpawnActor<APostProcessVolume>())
			{
				NightVol->bUnbound = true;
				NightVol->Priority = 1000.f; // wint van de PPV van het lighting-scenario
				NightVol->BlendWeight = 0.f;
				// Exposure remmen EN via color grading donker/koel maken: de gevels van de map zijn
				// gebakken/emissive verlicht, dus alleen lichten dimmen deed niks. Grading zit NA de
				// auto-exposure in de pipeline - die kan dit dus niet terugcompenseren.
				NightVol->Settings.bOverride_AutoExposureBias = true;
				NightVol->Settings.AutoExposureBias = -1.5f;
				NightVol->Settings.bOverride_ColorGain = true;
				NightVol->Settings.ColorGain = FVector4(0.45f, 0.5f, 0.7f, 1.f); // nachtblauw, zacht genoeg voor neon/lampen
				NightVol->Settings.bOverride_ColorSaturation = true;
				NightVol->Settings.ColorSaturation = FVector4(0.9f, 0.9f, 0.9f, 1.f);
				// Nacht-bloom OMHOOG: neon en borden bloeien dan weer mooi op de gevels (de lagere
				// nacht-exposure dempt de bloom-bleed anders te veel).
				NightVol->Settings.bOverride_BloomIntensity = true;
				NightVol->Settings.BloomIntensity = 1.3f;
				NightPPV = NightVol;
			}
		}
		// HEMEL-WAAS temmen: de Mie-nevel van de map-atmosfeer maakte een gigantisch wazig
		// zonsgebied (halve hemel wit). Minder nevel-dichtheid + halo strakker om de schijf.
		if (!FMath::IsNearlyEqual(LastAppliedHaze, SunHaze, 0.00002f))
		{
			for (TObjectIterator<USkyAtmosphereComponent> AtIt; AtIt; ++AtIt)
			{
				if (AtIt->GetWorld() != GetWorld()) { continue; }
				AtIt->SetMieScatteringScale(SunHaze);
				AtIt->SetMieAnisotropy(0.96f);
				LastAppliedHaze = SunHaze;
			}
		}

		if (NightPPV.IsValid())
		{
			NightPPV->BlendWeight = 1.f - MinDayF;
			NightPPV->Settings.AutoExposureBias = NightExposure;
			NightPPV->Settings.ColorGain = FVector4(0.82f * NightGain, 0.91f * NightGain, 1.27f * NightGain, 1.f);
		}
		// Bloom-rem ALLEEN overdag (tegen de zon-waas) - 's nachts volle bloei voor neon en lampen.
		if (BloomPPV.IsValid())
		{
			BloomPPV->BlendWeight = MinDayF;
			BloomPPV->Settings.BloomIntensity = DayBloom;
		}

		// Pack-lampen op kloktijd (aan vanaf 17u, uit om 8u). Falloff staat uit, dus de
		// intensiteit is unitless: slider-default 42000 wordt kegel 7.0 / poel 4.7.
		const bool bLampOn = MinDayF < 0.35f; // aan met de schemering, uit zodra het echt licht is
		for (UPointLightComponent* PL : PackLampLights)
		{
			if (!PL) { continue; }
			const float Want = bLampOn ? (PL->ComponentHasTag(TEXT("TallLamp")) ? LampIntensity / 6000.f : LampIntensity / 9000.f) : 0.f;
			if (!FMath::IsNearlyEqual(PL->Intensity, Want, 0.05f)) { PL->SetIntensity(Want); }
		}

		// BLOOM-REM (altijd aan): de zonneschijf blies via de bloom op tot een gigantische witte
		// waas over de halve hemel. Alleen bloom geremd, verder niks aan de look veranderd.
		if (!BloomPPV.IsValid())
		{
			if (APostProcessVolume* BloomVol = GetWorld()->SpawnActor<APostProcessVolume>())
			{
				BloomVol->bUnbound = true;
				BloomVol->Priority = 999.f;
				BloomVol->BlendWeight = 1.f;
				BloomVol->Settings.bOverride_BloomIntensity = true;
				BloomVol->Settings.BloomIntensity = 0.4f; // genoeg gloed voor neon/stoplichten, zonder zon-bom
				// Exposure-boost begrenzen: de auto-exposure krikte de scene zo ver op dat de lucht
				// rond de zon naar wit verzadigde - een gigantische gloeibal ipv een zonneschijf.
				BloomVol->Settings.bOverride_AutoExposureMaxBrightness = true;
				BloomVol->Settings.AutoExposureMaxBrightness = 1.0f;
				BloomPPV = BloomVol;
			}
		}

		// Het LIGHTING-SCENARIO van de map (Lighting_Sunny/Sunset sublevels: zon + skylight +
		// gevel-belichting) gaat 's nachts UIT - dan blijven donkere gebouwen met alleen de
		// lampen en emissives over: precies de nacht-look die de map zelf in zich heeft.
		// ALTIJD zichtbaar: de dimmer regelt de scenario-lichten al per type (zonnen 0, skylight
		// ramp), dus het level hoeft niet meer aan/uit - die harde toggle gaf om ~5:30 een
		// abrupte look-switch. Alle dag/nacht-overgangen lopen nu via vloeiende ramps.
		const bool bScenarioVisible = true;
		for (ULevelStreaming* SL : GetWorld()->GetStreamingLevels())
		{
			if (!SL) { continue; }
			if (!SL->GetWorldAssetPackageName().Contains(TEXT("/Maps/Lighting/"))) { continue; }
			SL->SetShouldBeVisible(bScenarioVisible);
		}

		// MAAN: eigen zacht blauw strijklicht bij nacht - de echte zon/maan-cyclus-look van vroeger.
		// Luistert naar de Moon-sliders in de Light-tab (MoonIntensity/MoonPitch), overdag op 0.
		if (!PackMoon.IsValid())
		{
			if (ADirectionalLight* M = GetWorld()->SpawnActor<ADirectionalLight>(ADirectionalLight::StaticClass(), FTransform(FRotator(MoonPitch, 200.f, 0.f))))
			{
				if (ULightComponent* MC = M->GetLightComponent())
				{
					MC->SetMobility(EComponentMobility::Movable);
					MC->SetIntensity(0.f);
					MC->SetLightColor(FLinearColor(0.55f, 0.65f, 0.9f));
					MC->SetCastShadows(true);
					if (UDirectionalLightComponent* MDL = Cast<UDirectionalLightComponent>(MC))
					{
						MDL->SetAtmosphereSunLight(true);   // maan-schijf aan de hemel
						MDL->SetAtmosphereSunLightIndex(1); // slot 1 = maan (0 = zon)
						MDL->SetLightSourceAngle(1.5f);
						MDL->ForwardShadingPriority = 0;
					}
				}
				PackMoon = M;
			}
		}
		// EIGEN BEWEGENDE ZON: volledige dagboog - op bij zonsopkomst, piek 's middags, onder
		// tussen NW en N (yaw 337.5), precies zoals op onze eigen map.
		if (!PackSun.IsValid())
		{
			if (ADirectionalLight* SunA = GetWorld()->SpawnActor<ADirectionalLight>(ADirectionalLight::StaticClass(), FTransform(FRotator(-45.f, 337.5f, 0.f))))
			{
				if (ULightComponent* SC2 = SunA->GetLightComponent())
				{
					SC2->SetMobility(EComponentMobility::Movable);
					SC2->SetCastShadows(true);
					SC2->SetIntensity(0.f);
					if (UDirectionalLightComponent* SDL = Cast<UDirectionalLightComponent>(SC2))
					{
						SDL->SetAtmosphereSunLight(true);
						SDL->SetLightSourceAngle(1.5f);
						SDL->SetLightSourceSoftAngle(0.f);
						SDL->ForwardShadingPriority = 10;
						SDL->MarkRenderStateDirty();
					}
				}
				PackSun = SunA;
			}
		}
		const float DayLen2 = FMath::Max(1.f, Sunset - Sunrise);
		if (PackSun.IsValid() && PackSun->GetLightComponent())
		{
			const float SunPhase2 = (Hour - Sunrise) / DayLen2;
			PackSun->SetActorRotation(FRotator(-180.f * SunPhase2, 337.5f, 0.f));
			PackSun->GetLightComponent()->SetIntensity(SunIntensity * MinDayF);
		}
		// MAAN: tegenboog door de nacht - op bij zonsondergang, onder bij zonsopkomst, zelfde N-lijn.
		if (PackMoon.IsValid() && PackMoon->GetLightComponent())
		{
			float NightF = 0.f;
			const float NightLen = FMath::Max(1.f, 24.f - DayLen2);
			if (Hour >= Sunset)      { NightF = (Hour - Sunset) / NightLen; }
			else if (Hour < Sunrise) { NightF = (Hour + 24.f - Sunset) / NightLen; }
			PackMoon->SetActorRotation(FRotator(-180.f * NightF, 337.5f, 0.f));
			PackMoon->GetLightComponent()->SetIntensity((1.f - MinDayF) * MoonIntensity);
		}
		// Fotokoepel (dag-lucht met ingebakken zon): 's nachts uit zodat de donkere hemel toont.
		// Foto-koepel PERMANENT uit: er zit een vaste zon in de foto gebakken - botst met
		// onze bewegende zon. De atmosfeer-lucht volgt nu de echte zonnestand.
		const bool bDomeVisible = false;
		for (const TWeakObjectPtr<UStaticMeshComponent>& Dc : DomeComps)
		{
			if (UStaticMeshComponent* MC = Dc.Get())
			{
				if (MC->IsVisible() != bDomeVisible) { MC->SetVisibility(bDomeVisible); }
			}
		}
		return;
	}

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

void ADayNightController::LoadLightConfig()
{
	TArray<FString> Lines;
	if (!FFileHelper::LoadFileToStringArray(Lines, *(FPaths::ProjectSavedDir() / TEXT("LightConfig.txt")))) { return; }
	for (const FString& Line : Lines)
	{
		FString K, V;
		if (!Line.Split(TEXT("="), &K, &V)) { continue; }
		const float F = FCString::Atof(*V);
		if      (K == TEXT("MoonIntensity")) { MoonIntensity = F; }
		else if (K == TEXT("SunIntensity"))  { SunIntensity = F; }
		else if (K == TEXT("SkyNight"))      { SkyNight = F; }
		else if (K == TEXT("SkyDay"))        { SkyDay = F; }
		else if (K == TEXT("MoonPitch"))     { MoonPitch = F; }
		else if (K == TEXT("LampIntensity")) { LampIntensity = F; }
		else if (K == TEXT("ExposureBias"))  { ExposureBias = F; }
		else if (K == TEXT("NightGain"))     { NightGain = F; }
		else if (K == TEXT("NightExposure")) { NightExposure = F; }
		else if (K == TEXT("DayBloom"))      { DayBloom = F; }
		else if (K == TEXT("SunHaze"))       { SunHaze = F; }
	}
	UE_LOG(LogTemp, Warning, TEXT("[LightConfig] geladen (%d regels)"), Lines.Num());
}
