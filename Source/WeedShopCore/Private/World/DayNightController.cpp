#include "World/DayNightController.h"
#include "WeedShopCore.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/LevelStreaming.h"
#include "Components/SkyAtmosphereComponent.h"

#include "World/DayCycleComponent.h"
#include "World/WorldSyncComponent.h"
#include "Game/WeedShopGameState.h"
#include "UI/WeedUiStyle.h" // D24: WeedUI::SoundCategoryVolume(3=VolWeather) voor de weer-volume-slider
#include "Save/AssetKeepAliveSubsystem.h" // keep-alive: string-geladen UDS/UDW-klasses niet per LoadMap laten purgen/hercompileren

#include "Engine/DirectionalLight.h"
#include "Components/DirectionalLightComponent.h"
#include "Engine/SkyLight.h"
#include "Components/SkyLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/LocalLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/ReflectionCaptureComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/DecalComponent.h"
#include "GameFramework/PlayerController.h" // licht-budget-pool: speler-positie voor de afstand-cap
#include "GameFramework/Pawn.h"
#include "Components/SkeletalMeshComponent.h" // cloth-afscherming op map-vlaggen (anti-crash)
#include "HAL/IConsoleManager.h" // licht-budget-pool: r.LightMaxDrawDistanceScale uitlezen om de hide-afstand op de render-fade te knopen
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h" // FProperty/FDoubleProperty + TFieldIterator voor de UDS-brug
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

static constexpr float GNightSkyFloor = 0.12f;
static constexpr float GNightExposureMinBrightness = 0.4f;
static constexpr float GUdsNightExposureFloor = -1.2f;
static constexpr float GUdsNightGlowFloor = 1.0f;

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
	// (De UDS-naar-middag-push die hier stond is VERWIJDERD: de kaart-capture is nu SCS_BaseColor
	//  (albedo, belichtings-onafhankelijk) dus de push was overbodig - en hij kostte ~370ms per
	//  kaart-open omdat UDW "Update Active Variables" de rain-componenten opnieuw opbouwde met
	//  blocking LoadAsset (gemeten met stat dumphitches, 07-03).)
	if (NightPPV.IsValid()) { NightPPV->BlendWeight = 0.f; }
	if (BloomPPV.IsValid()) { BloomPPV->BlendWeight = 1.f; }
	for (FDimLight& D : DimLights)
	{
		if (ULightComponent* LC = D.Light.Get())
		{
			if (LC->IsA(USkyLightComponent::StaticClass())) { LC->SetIntensity(D.OrigIntensity); }
		}
	}
	// (Lampen NIET meer op 0: de kaart-capture mag de straatverlichting tonen 's nachts -> leesbare nacht-kaart
	//  met dag/nacht-cyclus. Overdag staan de lampen toch uit via de klok, dus de dag-kaart blijft schoon.)
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
		TEXT("MoonIntensity=%.3f\nSunIntensity=%.3f\nSkyNight=%.3f\nSkyDay=%.3f\nMoonPitch=%.1f\nLampIntensity=%.0f\nExposureBias=%.2f\nNightGain=%.3f\nNightExposure=%.2f\nDayBloom=%.3f\nSunHaze=%.5f\nUdsExpDay=%.3f\nUdsExpDawnDusk=%.3f\nUdsExpNight=%.3f\nUdsCloud=%.3f\nUdsFog=%.3f\n"),
		MoonIntensity, SunIntensity, SkyNight, SkyDay, MoonPitch, LampIntensity, ExposureBias, NightGain, NightExposure, DayBloom, SunHaze, UdsExpDay, UdsExpDawnDusk, FMath::Max(UdsExpNight, GUdsNightExposureFloor), UdsCloud, UdsFog);
	const FString Path = FPaths::ProjectSavedDir() / TEXT("LightConfig.txt");
	FFileHelper::SaveStringToFile(Cfg, *Path);
	UE_LOG(LogTemp, Log, TEXT("[LightConfig] saved to %s\n%s"), *Path, *Cfg);
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

float ADayNightController::CeilingOnIntensity(const UPointLightComponent* PL) const
{
	if (!PL) { return 0.f; }
	// Zelfde schaal als de klok-loop (zie Tick): CeilLamp = hoofdkegel, CeilGlow = zachte room-fill.
	const float Div = PL->ComponentHasTag(TEXT("CeilGlow")) ? 2500.f : 900.f; // CeilLamp 900 = origineel (hoofdlamp dim, vloer/ramen niet blown); CeilGlow 9000->2500 = zachte plafond-glow opgehelderd zodat 't PLAFOND verlicht; TUNEBAAR
	return LampIntensity / Div;
}

void ADayNightController::CollectCeilingLightsNear(const FVector& Center, float Radius, TArray<UPointLightComponent*>& OutLights) const
{
	const float R2 = Radius * Radius;
	for (UPointLightComponent* PL : PackLampLights)
	{
		if (!PL) { continue; }
		if (!PL->ComponentHasTag(TEXT("CeilLamp")) && !PL->ComponentHasTag(TEXT("CeilGlow"))) { continue; }
		if (FVector::DistSquared(PL->GetComponentLocation(), Center) <= R2) { OutLights.Add(PL); }
	}
}

void ADayNightController::CollectCeilingEmisNear(const FVector& Center, float Radius, TArray<UMaterialInstanceDynamic*>& OutMids, TArray<float>& OutBright, TArray<FVector>& OutPos) const
{
	const float R2 = Radius * Radius;
	for (int32 i = 0; i < PackCeilEmis.Num(); ++i)
	{
		UMaterialInstanceDynamic* E = PackCeilEmis[i];
		if (!E || !PackCeilEmisPos.IsValidIndex(i)) { continue; }
		if (FVector::DistSquared(PackCeilEmisPos[i], Center) <= R2)
		{
			OutMids.Add(E);
			OutBright.Add(PackCeilEmisBright.IsValidIndex(i) ? PackCeilEmisBright[i] : 1.f);
			OutPos.Add(PackCeilEmisPos[i]);
		}
	}
}

void ADayNightController::GetCeilingLampPositions(TArray<FVector>& Out) const
{
	// Alleen de hoofd-kegel (CeilLamp) telt als "een lamp" - de glow zit op dezelfde plek.
	for (UPointLightComponent* PL : PackLampLights)
	{
		if (PL && PL->ComponentHasTag(TEXT("CeilLamp"))) { Out.Add(PL->GetComponentLocation()); }
	}
}

void ADayNightController::SetSwitchControlledLight(UPointLightComponent* PL, bool bControlled)
{
	if (!PL) { return; }
	if (bControlled) { SwitchControlledLights.Add(PL); }
	else { SwitchControlledLights.Remove(PL); }
}

void ADayNightController::SetSwitchControlledEmis(UMaterialInstanceDynamic* E, bool bControlled)
{
	if (!E) { return; }
	if (bControlled) { SwitchControlledEmis.Add(E); }
	else { SwitchControlledEmis.Remove(E); }
}

void ADayNightController::BeginPlay()
{
	Super::BeginPlay();

	UWorld* W = GetWorld();
	if (!W) { return; }

	LocalInstance = this; // bereikbaar voor de phone-UI (live tunen)

	LoadLightConfig(); // opgeslagen slider-waardes terugladen

	// Grafische keuze van de speler: opgeslagen kwaliteit-tier ALTIJD toepassen (boot = live). Voorheen
	// kreeg alleen Potato de tier-cvars bij boot -> Low/Med/High/Epic startten op kale sg.*-scalability
	// en dreven na een herstart weg van de preset. Daarna de losse vlag-overrides (Lumen/VSM/RT/
	// MotionBlur): eigen toggles die van de tier MOGEN afwijken, dus na de tier in deze volgorde.
	bool bVSMOff = false; // gehoist: de beach-tak heeft de shadows-vlag (VSMOff) nodig
	{
		FString GfxTxt;
		FFileHelper::LoadFileToString(GfxTxt, *WeedData::File(TEXT("GraphicsConfig.txt"))); // File() kopieert de gebakken config naar Saved/ op een verse install
		const bool bLumenOff = GfxTxt.Contains(TEXT("LumenOff=1"));
		const bool bMbOff = GfxTxt.Contains(TEXT("MotionBlurOff=1"));
		bVSMOff = GfxTxt.Contains(TEXT("VSMOff=1"));
		const bool bRTOff = GfxTxt.Contains(TEXT("RTOff=1"));
		// bSkipFeatureGates=true: Lumen/RT/VSM worden hieronder DIRECT met de user-toggle-waarden gezet;
		// de tier ze eerst ook laten zetten = VSM UIT->AAN pool-heralloc (~2-4s wereld-load) voor niks.
		WeedShop_ApplyGraphicsTier(WeedShop_ReadTier(), true); // volledige tier: scalability + console-prio cvars
		WeedShop_ApplyLumen(bLumenOff);
		WeedShop_ApplyVSM(bVSMOff);
		WeedShop_ApplyRayTracing(bRTOff);
		if (bMbOff) { WeedShop_ApplyMotionBlur(true); }
	}

	// PACK-MAPS: MINIMAL-modus - overdag blijft de stock-look 100% intact (geen zon/fog/scenario-
	// ingrepen; dat sloopte de art-stijl). 's Nachts dimmen we alleen de BESTAANDE lichten van de
	// map en gaat de HDRI-fotokoepel (dag-lucht) uit zodat de donkere hemel zichtbaar wordt.
	if (W->GetOutermost()->GetName().StartsWith(TEXT("/Game/CityBeachStrip")))
	{
		bPackMinimal = true;
		// VSM AAN. De echte crash-oorzaak (GPU-Scene reserved-resources) is opgelost in DefaultEngine.ini
		// [SystemSettings], dus VSM crasht niet meer ("Out of video memory" terwijl de GPU 70% leeg was). En VSM
		// is hier juist de OPLOSSING voor de lag: het CACHET schaduwen i.p.v. de hele stad elk frame x4 cascades
		// te hertekenen (was ~20.500 draw-calls, Draw-thread-bound = "super laggy richting de zon"). VSM = soepel
		// + betere kwaliteit (NPC-schaduwen, uitlijning). Console-prio zodat 't op de beach altijd aan staat.
				WeedShop_ApplyDistanceFieldGI(true); // zelfde OOM-familie: DF-AO bouwt een enorme brick-atlas op deze map
		WeedShop_ApplyBeachShadows(bVSMOff); // schaduwen aan/uit = de VSMOff-vlag (eigen Shadows-toggle, los van de Preset/Potato)
		SpawnUDS(); // UDS neemt zon/lucht/wolken/weer over; Tick voedt z'n tijd + dimt de gebakken gevels
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

	// (Oude per-NPC lantaarnpalen verwijderd: de stad-straatlampen op de map zijn nu de
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

void ADayNightController::SpawnUDS()
{
	UWorld* W = GetWorld();
	if (!W) { return; }
	UClass* UdsClass = LoadClass<AActor>(nullptr, TEXT("/Game/UltraDynamicSky/Blueprints/Ultra_Dynamic_Sky.Ultra_Dynamic_Sky_C"));
	if (!UdsClass)
	{
		UE_LOG(LogTemp, Verbose, TEXT("[UDS] Ultra_Dynamic_Sky class niet gevonden - UDS uit, klassieke zon/maan blijft"));
		return;
	}
	UAssetKeepAliveSubsystem::Keep(this, UdsClass); // keep-alive: anders purget GC de BP-keten per LoadMap -> herlaad/hercompile-hitch
	FActorSpawnParameters SP;
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AActor* Uds = W->SpawnActor<AActor>(UdsClass, FTransform::Identity, SP);
	if (!Uds) { UE_LOG(LogTemp, Verbose, TEXT("[UDS] spawn faalde")); return; }
	UdsSky = Uds;
	bUseUDS = true;
	UdsTimeProp = Uds->GetClass()->FindPropertyByName(FName(TEXT("Time of Day")));
	UdsUpdateFn = Uds->FindFunction(FName(TEXT("Update Active Variables")));
	UdsUpdateStaticFn = Uds->FindFunction(FName(TEXT("Update Static Variables")));
	UE_LOG(LogTemp, Verbose, TEXT("[UDS] gespawnd. TimeProp=%s UpdateFn=%s(parms=%d)"),
		UdsTimeProp ? *UdsTimeProp->GetClass()->GetName() : TEXT("NULL"),
		UdsUpdateFn ? TEXT("OK") : TEXT("NULL"),
		UdsUpdateFn ? UdsUpdateFn->NumParms : -1);

	// Environment-sounds zitten NIET op de sky-actor -> aparte UDS-geluidsactor spawnen (tijd/weer-gestuurde ambience).
	if (UClass* SndClass = LoadClass<AActor>(nullptr, TEXT("/Game/UltraDynamicSky/Blueprints/Sound/AmbientSound_Time_and_Weather_Controlled.AmbientSound_Time_and_Weather_Controlled_C")))
	{
		UAssetKeepAliveSubsystem::Keep(this, SndClass); // keep-alive: BP-keten niet per LoadMap opnieuw laden
		UdsSound = W->SpawnActor<AActor>(SndClass, FTransform::Identity, SP);
		UE_LOG(LogTemp, Verbose, TEXT("[UDS] sound-actor: %s"), UdsSound.IsValid() ? TEXT("OK") : TEXT("NULL"));
		// D24: cache de default "Volume Multiplier"-BP-var 1x zodat de weer-volume-slider (categorie 3) 'm
		// altijd vanaf de originele waarde kan schalen. -1 = var niet gevonden -> ApplyWeatherVolume valt
		// terug op de per-state-multipliers (zie daar). Beide float/double-varianten afvangen.
		if (AActor* Snd = UdsSound.Get())
		{
			if (FDoubleProperty* VmP = CastField<FDoubleProperty>(Snd->GetClass()->FindPropertyByName(FName(TEXT("Volume Multiplier")))))   { UdsSoundBaseVolMul = (float)VmP->GetPropertyValue_InContainer(Snd); }
			else if (FFloatProperty* VmPf = CastField<FFloatProperty>(Snd->GetClass()->FindPropertyByName(FName(TEXT("Volume Multiplier"))))) { UdsSoundBaseVolMul = VmPf->GetPropertyValue_InContainer(Snd); }
			UE_LOG(LogTemp, Verbose, TEXT("[UDS] sound Volume Multiplier default: %.3f (%s)"), UdsSoundBaseVolMul, UdsSoundBaseVolMul >= 0.f ? TEXT("gevonden") : TEXT("NIET gevonden - fallback"));
		}
	}
	else { UE_LOG(LogTemp, Verbose, TEXT("[UDS] sound-class niet gevonden")); }

	// Ultra Dynamic WEATHER zit OOK in deze pack -> spawnen voor ECHT weer (regen/sneeuw/storm/lightning).
	// UDW detecteert de Sky zelf en koppelt alles. Met UDW aanwezig stuurt het weer de cloud/fog van de Sky.
	if (UClass* WClass = LoadClass<AActor>(nullptr, TEXT("/Game/UltraDynamicSky/Blueprints/Ultra_Dynamic_Weather.Ultra_Dynamic_Weather_C")))
	{
		UAssetKeepAliveSubsystem::Keep(this, WClass); // keep-alive: BP-keten niet per LoadMap opnieuw laden
		UdsWeatherActor = W->SpawnActor<AActor>(WClass, FTransform::Identity, SP);
		UE_LOG(LogTemp, Verbose, TEXT("[UDS] UDW (weer) gespawnd: %s"), UdsWeatherActor.IsValid() ? TEXT("OK") : TEXT("NULL"));
		if (AActor* Udw = UdsWeatherActor.Get())
		{
			Udw->SetActorTickEnabled(true);
			// TEMP introspectie: UDW's weer-functies + Weather-preset/variatie-properties.
			for (TFieldIterator<UFunction> WFnIt(Udw->GetClass()); WFnIt; ++WFnIt)
			{
				const FString N = WFnIt->GetName();
				if (N.Contains(TEXT("Weather")) || N.Contains(TEXT("Change")) || N.Contains(TEXT("Random")) || N.Contains(TEXT("Preset")))
				{
					UE_LOG(LogTemp, Verbose, TEXT("[UDW-FUNC] %s (parms=%d)"), *N, WFnIt->NumParms);
				}
			}
			for (TFieldIterator<FProperty> WPrIt(Udw->GetClass()); WPrIt; ++WPrIt)
			{
				const FString N = WPrIt->GetName();
				if (N == TEXT("Weather") || N.Contains(TEXT("Random Weather")) || N.Contains(TEXT("Variation")) || N.Contains(TEXT("Cloud")) || N.Contains(TEXT("Coverage")) || N.Contains(TEXT("Manual")) || N.Contains(TEXT("Override")))
				{
					UE_LOG(LogTemp, Verbose, TEXT("[UDW-PROP] %s : %s"), *N, *WPrIt->GetClass()->GetName());
				}
			}

			SetRandomWeather(true); // natuurlijk wisselend weer aan bij start

			// D23 onweer temperen: de pack-default vuurt bij max thunder veel te veel bliksem-flashes
			// achter elkaar af. Frequentie omlaag (flitsen/min) + timing-randomisatie omhoog zodat de
			// flitsen minder metronoom/burst-achtig komen. Property-namen zijn UDW-versie-afhankelijk ->
			// SetUdwDouble null-guardt + logt per set of 'ie pakte (zie [UDW-SET] in de log).
			SetUdwDouble(FName(TEXT("Lightning Flash Frequency")), 2.5);          // flitsen/min bij max thunder (was veel hoger)
			SetUdwDouble(FName(TEXT("Lightning Flash Timing Randomization")), 0.85); // meer spreiding -> geen strakke bursts

			// D24: cache de UDW-donder-defaults 1x zodat de weer-volume-slider (categorie 3) ze kan schalen
			// zonder ze bij herhaald toepassen af te breken (altijd vanaf de originele waarde vermenigvuldigen).
			if (FDoubleProperty* CtP = CastField<FDoubleProperty>(Udw->GetClass()->FindPropertyByName(FName(TEXT("Close Thunder Volume")))))   { UdwCloseThunderBase = CtP->GetPropertyValue_InContainer(Udw); }
			else if (FFloatProperty* CtPf = CastField<FFloatProperty>(Udw->GetClass()->FindPropertyByName(FName(TEXT("Close Thunder Volume"))))) { UdwCloseThunderBase = CtPf->GetPropertyValue_InContainer(Udw); }
			if (FDoubleProperty* DtP = CastField<FDoubleProperty>(Udw->GetClass()->FindPropertyByName(FName(TEXT("Distant Thunder Volume")))))   { UdwDistantThunderBase = DtP->GetPropertyValue_InContainer(Udw); }
			else if (FFloatProperty* DtPf = CastField<FFloatProperty>(Udw->GetClass()->FindPropertyByName(FName(TEXT("Distant Thunder Volume"))))) { UdwDistantThunderBase = DtPf->GetPropertyValue_InContainer(Udw); }
			UE_LOG(LogTemp, Verbose, TEXT("[UDW] donder-defaults: close=%.3f distant=%.3f"), UdwCloseThunderBase, UdwDistantThunderBase);
		}
	}
	else { UE_LOG(LogTemp, Verbose, TEXT("[UDS] UDW-class niet gevonden")); }

	if (AActor* U2 = UdsSky.Get()) { U2->SetActorTickEnabled(true); } // UDS' eigen update-loop draaien
	// Perf/VRAM: de real-time sky-light-capture over meerdere frames spreiden i.p.v. elke frame de hele
	// cubemap -> lagere VRAM-piek + minder GPU-stoot (scheelt marge t.o.v. zware shadows).
	SetUdsBool(FName(TEXT("Real Time Capture Uses Time Slicing")), true);

	// Realistische sterren + Melkweg/nebula. Render Nebula stond default UIT; sterren waren te zwak (0.75).
	SetUdsBool(FName(TEXT("Simulate Real Stars")), true);   // 360-graden hi-res starmap
	SetUdsBool(FName(TEXT("Render Nebula")), true);         // nebula AAN (was uit); intensiteiten via ApplyUdsLook + sliders

	// Aurora: 2D-aurora (Use Auroras) draait NAAST de wolken. TEST: geforceerd AAN; daarna ~15% random.
	const bool bAurora = (FMath::FRand() < 0.15f); // ~15% kleine kans op aurora deze sessie
	SetUdsBool(FName(TEXT("Use Auroras")), bAurora);
	// Eenmalige static-rebuild bij spawn: bouwt de nacht-lucht-material op (sterren-texture, nebula, aurora).
	// Cloud Coverage raken we niet aan -> default-wolken blijven heel.
	if (UdsUpdateStaticFn && UdsUpdateStaticFn->NumParms == 0)
	{
		Uds->ProcessEvent(UdsUpdateStaticFn, nullptr);
	}
	UE_LOG(LogTemp, Verbose, TEXT("[UDS] aurora deze sessie: %s"), bAurora ? TEXT("JA") : TEXT("nee"));
	// TEMP introspectie: star/glow-properties MET huidige waarde (om intensiteit/glow te kunnen tunen).
	for (TFieldIterator<FProperty> SAIt(Uds->GetClass()); SAIt; ++SAIt)
	{
		const FString N = SAIt->GetName();
		if (!(N.Contains(TEXT("Star")) || N.Contains(TEXT("Simulate")) || N.Contains(TEXT("Night Sky Glow")) || N.Contains(TEXT("Twinkle")) || N.Contains(TEXT("Nebula")))) { continue; }
		if (FDoubleProperty* DP = CastField<FDoubleProperty>(*SAIt)) { UE_LOG(LogTemp, Verbose, TEXT("[UDS-SA] %s = %.3f"), *N, DP->GetPropertyValue_InContainer(Uds)); }
		else if (FBoolProperty* BP = CastField<FBoolProperty>(*SAIt)) { UE_LOG(LogTemp, Verbose, TEXT("[UDS-SA] %s = %s"), *N, BP->GetPropertyValue_InContainer(Uds) ? TEXT("true") : TEXT("false")); }
		else { UE_LOG(LogTemp, Verbose, TEXT("[UDS-SA] %s : %s"), *N, *SAIt->GetClass()->GetName()); }
	}

	ApplyUdsLook(); // belichting; UDS houdt z'n eigen wolken
}

void ADayNightController::SetUdsDouble(FName P, double V)
{
	AActor* Uds = UdsSky.Get();
	if (!Uds) { return; }
	if (FProperty* Prop = Uds->GetClass()->FindPropertyByName(P))
	{
		if (FDoubleProperty* DP = CastField<FDoubleProperty>(Prop)) { DP->SetPropertyValue_InContainer(Uds, V); }
		else if (FFloatProperty* FP = CastField<FFloatProperty>(Prop)) { FP->SetPropertyValue_InContainer(Uds, (float)V); }
	}
}

void ADayNightController::SetUdsBool(FName P, bool V)
{
	AActor* Uds = UdsSky.Get();
	if (!Uds) { return; }
	if (FBoolProperty* BP = CastField<FBoolProperty>(Uds->GetClass()->FindPropertyByName(P))) { BP->SetPropertyValue_InContainer(Uds, V); }
}

// Spiegel van SetUdsDouble maar op de UDW-actor. Property-namen zijn UDW-versie-afhankelijk -> null-guard
// + 1 Verbose-log per set zodat we in de log kunnen zien of de naam bestond (pakte) of niet.
void ADayNightController::SetUdwDouble(FName P, double V)
{
	AActor* Udw = UdsWeatherActor.Get();
	if (!Udw) { UE_LOG(LogTemp, Verbose, TEXT("[UDW-SET] geen UDW-actor voor '%s'"), *P.ToString()); return; }
	if (FProperty* Prop = Udw->GetClass()->FindPropertyByName(P))
	{
		if (FDoubleProperty* DP = CastField<FDoubleProperty>(Prop)) { DP->SetPropertyValue_InContainer(Udw, V); UE_LOG(LogTemp, Verbose, TEXT("[UDW-SET] %s = %.3f (double)"), *P.ToString(), V); }
		else if (FFloatProperty* FP = CastField<FFloatProperty>(Prop)) { FP->SetPropertyValue_InContainer(Udw, (float)V); UE_LOG(LogTemp, Verbose, TEXT("[UDW-SET] %s = %.3f (float)"), *P.ToString(), V); }
		else { UE_LOG(LogTemp, Verbose, TEXT("[UDW-SET] '%s' bestaat maar is geen float/double (%s)"), *P.ToString(), *Prop->GetClass()->GetName()); }
	}
	else { UE_LOG(LogTemp, Verbose, TEXT("[UDW-SET] property '%s' niet gevonden op UDW"), *P.ToString()); }
}

// D24: pas de weer-volume-slider (0..1) toe op de UDS-sound-ambience + UDW-donder. Altijd vanaf de gecachte
// defaults schalen (herhaald toepassen breekt de waarde anders af). Route wordt 1x per wijziging gelogd.
void ADayNightController::ApplyWeatherVolume(float Vol01)
{
	Vol01 = FMath::Clamp(Vol01, 0.f, 1.f);

	// 1) UDS environment-sound: de BP-var "Volume Multiplier" op de gecachte default * slider zetten en de
	//    BP z'n eigen "Update Volume Multiplier" laten uitvoeren (past de lopende ambience-loop live aan).
	if (AActor* Snd = UdsSound.Get())
	{
		if (UdsSoundBaseVolMul >= 0.f)
		{
			const float Want = UdsSoundBaseVolMul * Vol01;
			if (FDoubleProperty* VmP = CastField<FDoubleProperty>(Snd->GetClass()->FindPropertyByName(FName(TEXT("Volume Multiplier")))))   { VmP->SetPropertyValue_InContainer(Snd, (double)Want); }
			else if (FFloatProperty* VmPf = CastField<FFloatProperty>(Snd->GetClass()->FindPropertyByName(FName(TEXT("Volume Multiplier"))))) { VmPf->SetPropertyValue_InContainer(Snd, Want); }
			if (UFunction* UpdFn = Snd->GetClass()->FindFunctionByName(FName(TEXT("Update Volume Multiplier"))))
			{
				if (UpdFn->NumParms == 0) { Snd->ProcessEvent(UpdFn, nullptr); }
				UE_LOG(LogTemp, Verbose, TEXT("[UDS-VOL] Volume Multiplier=%.3f (base %.3f * %.2f), Update Volume Multiplier %s"), Want, UdsSoundBaseVolMul, Vol01, UpdFn->NumParms == 0 ? TEXT("aangeroepen") : TEXT("verkeerde parms - overgeslagen"));
			}
			else { UE_LOG(LogTemp, Verbose, TEXT("[UDS-VOL] 'Update Volume Multiplier' niet gevonden - waarde gezet maar niet ge-refresht")); }
		}
		else
		{
			// Fallback: "Volume Multiplier" bestond niet op deze pack-versie. Log de route zodat we het zien;
			// een per-state-schaling zonder die var is niet betrouwbaar zonder de exacte BP-namen -> hier stoppen.
			UE_LOG(LogTemp, Verbose, TEXT("[UDS-VOL] geen 'Volume Multiplier'-var -> UDS-ambience niet geschaald (fallback-route)"));
		}
	}

	// 2) UDW-donder mee-schalen (triviaal): de gecachte close/distant-thunder-defaults * slider.
	if (UdwCloseThunderBase >= 0.0)   { SetUdwDouble(FName(TEXT("Close Thunder Volume")), UdwCloseThunderBase * Vol01); }
	if (UdwDistantThunderBase >= 0.0) { SetUdwDouble(FName(TEXT("Distant Thunder Volume")), UdwDistantThunderBase * Vol01); }
}

void ADayNightController::CallUdsUpdate()
{
	AActor* Uds = UdsSky.Get();
	if (Uds && UdsUpdateFn && UdsUpdateFn->NumParms == 0) { Uds->ProcessEvent(UdsUpdateFn, nullptr); }
}

void ADayNightController::ApplyUdsLook()
{
	if (!UdsSky.IsValid()) { return; }
	SetUdsBool(FName(TEXT("Apply Exposure Settings")), true); // UDS stuurt de exposure (UIT zetten blies de vloer wit + maakte de dag donker -> de UDS-exposure is juist correct voor de UDS-sky)
	SetUdsDouble(FName(TEXT("Exposure Bias Day")), UdsExpDay);
	SetUdsDouble(FName(TEXT("Exposure Bias Dawn/Dusk")), UdsExpDawnDusk);
	// D.5 min-licht-vloer: clamp de ONDERKANT vlak voor UDS zodat de nacht nooit volledig zwart wordt
	// (rond 01:00 zakte de MANUAL exposure naar zwart). Sliders blijven werken; alleen de bodem is begrensd.
	SetUdsDouble(FName(TEXT("Exposure Bias Night")), FMath::Max(UdsExpNight, GUdsNightExposureFloor));
	SetUdsDouble(FName(TEXT("Extra Night Brightness when Cloudy")), UdsExtraNightCloudy);
	SetUdsDouble(FName(TEXT("Stars Intensity")), UdsStars);
	SetUdsDouble(FName(TEXT("Nebula Intensity")), UdsNebula);
	SetUdsDouble(FName(TEXT("Night Sky Glow")), FMath::Max(UdsNightGlow, GUdsNightGlowFloor));
	// Cloud Coverage/Fog bewust NIET op de Sky aanraken (UDW stuurt die via de weer-state).
	CallUdsUpdate();
}

void ADayNightController::SetUdsWeather(int32 WeatherType)
{
	if (!UdsSky.IsValid()) { return; }
	UdsWeather = WeatherType;
	double Rain = 0.0, Snow = 0.0;
	switch (WeatherType)
	{
	case 1: UdsCloud = 0.65f; UdsFog = 0.05f; break;             // Cloudy
	case 2: UdsCloud = 0.85f; UdsFog = 0.25f; Rain = 0.8; break; // Rain
	case 3: UdsCloud = 1.0f;  UdsFog = 0.15f; Rain = 1.0; break; // Storm
	case 4: UdsCloud = 0.8f;  UdsFog = 0.2f;  Snow = 0.9; break; // Snow
	case 5: UdsCloud = 0.35f; UdsFog = 0.85f; break;             // Fog
	default: UdsCloud = 0.15f; UdsFog = 0.0f; break;             // Clear (0)
	}
	SetUdsDouble(FName(TEXT("UDW Rain Value")), Rain);
	SetUdsDouble(FName(TEXT("UDW Snow Value")), Snow);
	ApplyUdsLook(); // zet cloud/fog/exposure + update; sliders blijven in sync via UdsCloud/UdsFog
	UE_LOG(LogTemp, Verbose, TEXT("[UDS] weer=%d (cloud=%.2f fog=%.2f rain=%.2f snow=%.2f)"), WeatherType, UdsCloud, UdsFog, Rain, Snow);
}

void ADayNightController::SetWeatherPreset(const FString& PresetName, double TransitionSeconds)
{
	AActor* Udw = UdsWeatherActor.Get();
	if (!Udw) { return; }
	const FString Path = FString::Printf(TEXT("/Game/UltraDynamicSky/Blueprints/Weather_Effects/Weather_Presets/%s.%s"), *PresetName, *PresetName);
	// Preset-cache: LoadObject bij elke weer-wissel = mogelijke sync-load-hitch runtime. 1x laden + Keep
	// (overleeft GC-purge per LoadMap), daarna alleen de map-lookup.
	static TMap<FString, TWeakObjectPtr<UObject>> GPresetCache;
	UObject* Preset = nullptr;
	if (TWeakObjectPtr<UObject>* Found = GPresetCache.Find(Path)) { Preset = Found->Get(); }
	if (!Preset)
	{
		Preset = LoadObject<UObject>(nullptr, *Path);
		if (Preset)
		{
			UAssetKeepAliveSubsystem::Keep(this, Preset);
			GPresetCache.Add(Path, Preset);
		}
	}
	if (!Preset) { UE_LOG(LogTemp, Verbose, TEXT("[UDW] preset niet gevonden: %s"), *PresetName); return; }
	if (TransitionSeconds <= 0.0) { TransitionSeconds = 120.0; } // dev-knop zonder tijd -> nette graduele default
	UFunction* Fn = Udw->FindFunction(FName(TEXT("Change Weather")));
	if (!Fn) { UE_LOG(LogTemp, Verbose, TEXT("[UDW] Change Weather-functie niet gevonden")); return; }
	// DE OORZAAK VAN DE "SNAP": `Weather Speed` is een globale snelheids-multiplier op de UDW-actor voor ÁLLE
	// weer-veranderingen (incl. Change Weather; bron: UDW-docs + decompiled asset). Stond 'ie >1 (pack-default),
	// dan comprimeerde 't onze 8s-transitie naar ~1s. Forceren op 1.0 zodat "Time To Transition (Seconds)" letterlijk
	// in seconden geldt. (Log de oude waarde even ter bevestiging.)
	if (FProperty* WSP = Udw->GetClass()->FindPropertyByName(FName(TEXT("Weather Speed"))))
	{
		if (FDoubleProperty* WD = CastField<FDoubleProperty>(WSP)) { UE_LOG(LogTemp, Verbose, TEXT("[UDW] Weather Speed was %.2f -> 1.0"), WD->GetPropertyValue_InContainer(Udw)); WD->SetPropertyValue_InContainer(Udw, 1.0); }
		else if (FFloatProperty* WF = CastField<FFloatProperty>(WSP)) { UE_LOG(LogTemp, Verbose, TEXT("[UDW] Weather Speed was %.2f -> 1.0"), WF->GetPropertyValue_InContainer(Udw)); WF->SetPropertyValue_InContainer(Udw, 1.0f); }
	}
	// Change Weather heeft 2 params: "New Weather Type" (object=preset) + "Time To Transition To New Weather (Seconds)"
	// (double, ECHTE seconden). Generiek op type gezet (1 object + 1 double in de signatuur). overgang = TransitionSeconds (variabel);
	// UDW bezit de cloud-coverage, dus de wolken bouwen vanzelf over die tijd op (NIET zelf UDS Cloud Coverage zetten).
	uint8* Buf = (uint8*)FMemory_Alloca(FMath::Max<int32>((int32)Fn->ParmsSize, 1));
	FMemory::Memzero(Buf, Fn->ParmsSize);
	for (TFieldIterator<FProperty> It(Fn); It; ++It)
	{
		FProperty* P = *It;
		if (!(P->PropertyFlags & CPF_Parm) || (P->PropertyFlags & CPF_ReturnParm)) { continue; }
		if (FObjectPropertyBase* OP = CastField<FObjectPropertyBase>(P)) { OP->SetObjectPropertyValue_InContainer(Buf, Preset); }
		else if (FDoubleProperty* DP = CastField<FDoubleProperty>(P)) { DP->SetPropertyValue_InContainer(Buf, TransitionSeconds); }
		else if (FFloatProperty* FP = CastField<FFloatProperty>(P)) { FP->SetPropertyValue_InContainer(Buf, (float)TransitionSeconds); }
	}
	Udw->ProcessEvent(Fn, Buf);
	UE_LOG(LogTemp, Verbose, TEXT("[UDW] Change Weather -> %s (overgang %.0fs)"), *PresetName, TransitionSeconds);
}

void ADayNightController::SetRandomWeather(bool bOn)
{
	bAutoWeather = bOn;
	// UDW's EIGEN random-variatie altijd UIT (byte 0) -> wij kiezen het weer zelf, gewogen, per in-game dag.
	if (AActor* Udw = UdsWeatherActor.Get())
	{
		if (FByteProperty* RVProp = CastField<FByteProperty>(Udw->GetClass()->FindPropertyByName(FName(TEXT("Random Weather Variation"))))) { RVProp->SetPropertyValue_InContainer(Udw, 0); }
	}
	if (bOn) { WeatherTimer = 0.f; } // forceer een nieuwe keuze in de eerstvolgende Tick
	UE_LOG(LogTemp, Verbose, TEXT("[UDW] auto-weer (gewogen, per dag) -> %s"), bOn ? TEXT("aan") : TEXT("uit"));
}

// Gewogen weer-presets. DETERMINISTISCHE VOLGORDE: de index in deze tabel is het gedeelde co-op-id (H.12) dat de
// server via WorldSync naar de clients schrijft; server EN client mappen dezelfde index -> dezelfde preset-naam.
// Nooit herordenen zonder je te realiseren dat oude replicatie-indices dan een ander preset raken.
struct FWeatherPreset { const TCHAR* Name; float Weight; bool bBad; };
static const FWeatherPreset GWeatherPresets[] = {
	{ TEXT("Clear_Skies"), 4.0f, false }, { TEXT("Partly_Cloudy"), 4.0f, false }, { TEXT("Cloudy"), 3.0f, false },
	{ TEXT("Overcast"), 2.0f, false }, { TEXT("Foggy"), 2.0f, false },
	{ TEXT("Rain_Light"), 0.7f, true }, { TEXT("Rain"), 0.4f, true }, { TEXT("Rain_Thunderstorm"), 0.15f, true },
	{ TEXT("Snow_Light"), 0.12f, true }, { TEXT("Snow"), 0.06f, true }, { TEXT("Snow_Blizzard"), 0.03f, true },
};
static constexpr int32 GNumWeatherPresets = (int32)UE_ARRAY_COUNT(GWeatherPresets);

void ADayNightController::ApplyWeatherByIndex(int32 Index, float TransitionSeconds)
{
	// Server EN client landen hier: index -> preset-naam via dezelfde deterministische tabel -> identiek weer.
	if (Index < 0 || Index >= GNumWeatherPresets) { return; }
	SetWeatherPreset(FString(GWeatherPresets[Index].Name), (double)TransitionSeconds);
}

void ADayNightController::PickAndApplyWeather()
{
	// Gewogen: overwegend helder/bewolkt/mistig (~91%); regen ~7%; sneeuw/storm zeldzaam ~1,5% (beach).
	float Total = 0.f; for (const FWeatherPreset& E : GWeatherPresets) { Total += E.Weight; }
	float R = FMath::FRandRange(0.f, Total);
	int32 PickedIdx = 0;
	for (int32 i = 0; i < GNumWeatherPresets; ++i) { R -= GWeatherPresets[i].Weight; if (R <= 0.f) { PickedIdx = i; break; } }
	const float TransSecs = FMath::FRandRange(120.f, 240.f); // overgang clear->rainstorm e.d. = 2-4 min graduele opbouw (variabel)
	// Slecht weer blijft kort, maar nu mét de 20s graduele opbouw erin: 45-65s = ~20s wolken-opbouw + ~25-45s
	// echte regen/sneeuw + daarna weer een graduele overgang weg. Mooi weer langer.
	WeatherTimer = TransSecs + (GWeatherPresets[PickedIdx].bBad ? FMath::FRandRange(60.f, 140.f) : FMath::FRandRange(150.f, 320.f)); // = overgang + settled-tijd -> overgang nooit afgekapt
	// Co-op-sync (H.12): de SERVER schrijft z'n keuze naar WorldSync zodat elke joiner exact hetzelfde weer leest
	// (de keuze valt alleen op de echte server; zie de guard in Tick). Lokaal meteen toepassen.
	if (UWorldSyncComponent* WS = GetWorldSync())
	{
		WS->SetWeather(PickedIdx, TransSecs);
		LastSeenWeatherIndex = PickedIdx; // eigen schrijf-actie niet als "wijziging" opnieuw toepassen op de host
	}
	ApplyWeatherByIndex(PickedIdx, TransSecs);
}

UWorldSyncComponent* ADayNightController::GetWorldSync() const
{
	// Spiegelt de CityDoor-cache: WorldSync 1x resolven en cachen (her-resolven zodra invalid).
	if (!CachedWorldSync.IsValid())
	{
		const AWeedShopGameState* GS = GetWorld() ? GetWorld()->GetGameState<AWeedShopGameState>() : nullptr;
		CachedWorldSync = GS ? GS->GetWorldSync() : nullptr;
	}
	return CachedWorldSync.Get();
}

void ADayNightController::DriveUDS(float ClockHour)
{
	AActor* Uds = UdsSky.Get();
	if (!Uds) { return; }
	const float Tod = FMath::Fmod(FMath::Fmod(ClockHour, 24.f) + 24.f, 24.f) * 100.f; // 0..2400
	// SMOOTH ZON (was throttle 3.0 = ~0,45°-stappen = zichtbaar getik + de player-schaduw die niet meebewoog). De
	// flikker die de throttle ooit verborg zat in de 512-page-pool (nu 8192) - NIET in de bewegende zon. Dus de zon
	// mag weer vloeiend: 0.05 ToD (~0,0075°/stap = onzichtbaar) = praktisch elk frame, maar capt de UDS-update + VSM-
	// re-render op ~27Hz (30-min dag) zodat de game-thread niet elk frame de hele UDS-BP draait. Bron: Fortnite-VSM-blog.
	if (FMath::IsNearlyEqual(Tod, LastUdsTod, 0.05f)) { return; }
	LastUdsTod = Tod;
	if (UdsTimeProp)
	{
		if (FDoubleProperty* DP = CastField<FDoubleProperty>(UdsTimeProp)) { DP->SetPropertyValue_InContainer(Uds, Tod); }
		else if (FFloatProperty* FP = CastField<FFloatProperty>(UdsTimeProp)) { FP->SetPropertyValue_InContainer(Uds, Tod); }
	}
	if (UdsUpdateFn && UdsUpdateFn->NumParms == 0) { Uds->ProcessEvent(UdsUpdateFn, nullptr); }
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
		if (bUseUDS)
		{
			DriveUDS(Hour); // UDS' lucht volgt onze gerepliceerde klok
			if (bAutoWeather)
			{
				// Co-op-sync (H.12): de DayNightController is per-proces niet-gerepliceerd, dus de weer-KEUZE mag
				// ALLEEN op de echte server vallen (NM_Client-check, NIET HasAuthority - die is ook true op de joiner).
				// De client kiest NIET zelf maar leest de door de server geschreven index uit WorldSync en past 'm
				// toe zodra 'ie wijzigt -> host en joiner tonen exact hetzelfde weer.
				const bool bIsServer = GetWorld() && GetWorld()->GetNetMode() != NM_Client;
				if (bIsServer)
				{
					WeatherTimer -= DeltaSeconds;
					if (WeatherTimer <= 0.f) { PickAndApplyWeather(); } // mooi weer ~2,5 in-game uur, slecht weer ~20-30 in-game min
				}
				else if (UWorldSyncComponent* WS = GetWorldSync())
				{
					const int32 SrvIdx = WS->GetWeatherIndex();
					if (SrvIdx >= 0 && SrvIdx != LastSeenWeatherIndex)
					{
						// Eerste sync na (late) join: de client staat nog op het UDW-default-weer (helder/te fel) en moet
						// METEEN naar de server-staat i.p.v. 2-4 min traag overvloeien (anders ziet de joiner minutenlang
						// lichter/ander weer dan de host = de uitgewassen look). Snap-in bij de eerste sync; latere
						// server-wijzigingen vloeien wel normaal over met de gerepliceerde duur.
						const bool bFirstSync = (LastSeenWeatherIndex < 0);
						LastSeenWeatherIndex = SrvIdx;
						ApplyWeatherByIndex(SrvIdx, bFirstSync ? 3.f : WS->GetWeatherDuration());
					}
				}
			}

			// D24 weer-volume-slider: elke 0.5s de settings-waarde (categorie 3 = VolWeather) lezen en alleen
			// bij een echte wijziging op de UDS-ambience + UDW-donder toepassen (client-side, geen replicatie).
			WeatherVolTimer -= DeltaSeconds;
			if (WeatherVolTimer <= 0.f)
			{
				WeatherVolTimer = 0.5f;
				const float WVol = WeedUI::SoundCategoryVolume(3);
				if (!FMath::IsNearlyEqual(WVol, LastWeatherVol, 0.005f))
				{
					LastWeatherVol = WVol;
					ApplyWeatherVolume(WVol);
				}
			}
		}

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
			LightScanTimer = 6.f; // streamende lichten worden binnen 6s opgepikt; halve scan-kost
			UWorld* W2 = GetWorld();
			const int32 SeenBefore = SeenLights.Num() + SeenRefCaps.Num() + SeenSky.Num() + PackLampSeen.Num();
			for (TActorIterator<AActor> It(W2); It; ++It)
			{
				AActor* A = *It;
				if (!IsValid(A)) { continue; }
				if (A == PackMoon.Get() || A == PackSun.Get() || A == this || A == UdsSky.Get()) { continue; } // eigen zon/maan/lampen + UDS niet mee-dimmen
				// PREVIEW-STUDIO'S (bv. de wardrobe-capture + kloon): eigen key/fill-lampen, bewust VOLLEDIG los
				// van dag/nacht. Zonder deze skip verborg de scan die runtime-lampen (niet-Movable-tak hieronder)
				// -> de wardrobe-preview werd na een paar seconden donker. Vóór de seen-set: elke nieuwe spawn
				// (herhaald openen/sluiten) wordt zo overgeslagen zonder in LightScanSeenActors te lekken.
				if (A->ActorHasTag(TEXT("NoDayNight"))) { continue; }
				// AL VERWERKT? Volledig overslaan: anders doen we per scan voor ELKE actor opnieuw de dure
				// component-gather + mesh-naam-string-checks (= de periodieke hang). Map-actors krijgen runtime
				// geen nieuwe componenten, dus 1x verwerken volstaat; nieuw-gestreamde actors zijn nog niet seen.
				if (LightScanSeenActors.Contains(A)) { continue; }
				LightScanSeenActors.Add(A);
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
						// FUNDAMENTELE PERF-FIX (deep-perf-systemic-audit 2026-06-27): NIET meer elke static map-light naar
						// Movable promoten. Dát maakte ~1300 authored static lights DYNAMISCH = "Lights in scene 1416" =
						// Lighting 4,3ms + InitViews 2,15ms = de #1 render-thread-kost. r.AllowStaticLighting=False -> die
						// static lights geven toch GEEN gebakken licht; de raam-GLOW zit in EMISSIVE materials (niet in deze
						// lights) en de speler-lampen zijn de controller's eigen PackLampLights (straat/plafond, blijven werken).
						// Static map-lights met rust laten -> ze vallen uit de dynamische light-count. Alleen al-Movable verwerken.
						if (LLC->Mobility != EComponentMobility::Movable) { LLC->SetVisibility(false); continue; } // static map-light (geen gebakken licht = GEEN licht) -> uit FScene = uit de light-count
						// VERRE LAMPEN CULLEN (grootste perf-winst): de beach had ~1416 actieve lampen -> Lighting
						// ~11ms = #1 render-thread-kost (stat scenerendering). Basis 350m; r.LightMaxDrawDistanceScale
						// (per graphics-preset = view-distance-factor, gezet in WeedShop_ApplyGraphicsTier) schaalt mee
						// -> potato ~120m, high ~350m. 30m fade = smooth uit, geen pop-in. Alleen point/spot
						// (ULocalLightComponent); zon/maan (UDirectionalLightComponent) gaan via de andere tak = globaal.
						LLC->SetMaxDrawDistance(25000.f);
						LLC->SetMaxDistanceFadeRange(3000.f);
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
				// REFLECTION CAPTURES verzamelen (sphere/box reflection capture actors van de map):
				// overdag gebakken -> 's nachts dimmen we hun brightness mee met de klok.
				TInlineComponentArray<UReflectionCaptureComponent*> Caps(A);
				for (UReflectionCaptureComponent* RC : Caps)
				{
					if (!RC || SeenRefCaps.Contains(RC)) { continue; }
					SeenRefCaps.Add(RC);
					FRefCap RcEntry; RcEntry.Cap = RC; RcEntry.OrigBrightness = RC->Brightness;
					// METEEN dimmen op de huidige klok-stand. (Anders blijft een capture die LATER instreamt
					// op dag-helderheid staan: de periodieke dim draait alleen als MinDayF verandert.)
					RC->Brightness = RcEntry.OrigBrightness * MinDayF;
					RC->MarkRenderStateDirty();
					RefCaps.Add(RcEntry);
				}
				// DECALS: 1401 op de beach (856 in beeld) = dure deferred-decal-pass. De per-decal FadeScreenSize staat
				// op de CDO-default 0.01 -> r.Decal.FadeScreenSizeMult (x4) bijt amper. Basis omhoog naar 0.07 zodat
				// verre/kleine decals echt uitfaden (x4 duwt ze dan tot FadeAlpha 0 = uit de decal-pass). 1x per decal; 6s-scan vangt gestreamde mee.
				TInlineComponentArray<UDecalComponent*> Decals(A);
				for (UDecalComponent* DecalC : Decals)
				{
					if (!DecalC || SeenDecals.Contains(DecalC)) { continue; }
					SeenDecals.Add(DecalC);
					DecalC->SetFadeScreenSize(0.12f); // hoger = meer cullen (stapelt op de cvar-flip). Knob: terug naar 0.10 als decals vlak bij je voeten poppen
				}
				// ANTI-CRASH: map-decoraties met Chaos-Cloth (bv. de wapperende vlag SM_Flag_01) her-creeren hun
				// clothing-actors bij elke re-register -> RenderCore/RHI access-violation (gedocumenteerd; log toonde
				// 'Recreating Clothing Actors for SM_Flag_01' spam vlak voor een RHI-crash). De crash-functie
				// RecreateClothingActors() draait ALLEEN als bAllowClothActors -> SetAllowClothActors(false) skipt 'm
				// helemaal (zelfde fix als de crowd-NPC's). De vlag blijft staan, wappert alleen niet meer. Pawns
				// (NPC's/speler) hebben hun eigen cloth-afscherming en slaan we over.
				if (!A->IsA(APawn::StaticClass()))
				{
					TInlineComponentArray<USkeletalMeshComponent*> Skels(A);
					for (USkeletalMeshComponent* SkC : Skels)
					{
						if (!SkC) { continue; }
						SkC->SetAllowClothActors(false);    // geen cloth-actors -> RecreateClothingActors() early-out -> geen crash
						SkC->bDisableClothSimulation = true; // + sim uit (belt-and-suspenders)
					}
				}
				// SKYLIGHTS apart verzamelen: USkyLightComponent is GEEN ULightComponent, dus de
				// licht-scan hierboven miste 'm. De movable skylight spiegelt z'n hemel-cubemap als
				// specular op water/ramen -> op volle dag-sterkte een felle "zon" 's nachts. Dimmen.
				TInlineComponentArray<USkyLightComponent*> SkyComps(A);
				for (USkyLightComponent* SLC : SkyComps)
				{
					if (!SLC || SeenSky.Contains(SLC)) { continue; }
					SeenSky.Add(SLC);
					// REAL-TIME CAPTURE aan: anders gebruikt de skylight een STATISCHE (gebakken) hemel-
					// cubemap -> de reflectie zit muurvast op één plek en toont een oude zon, ongeacht waar
					// onze zon/maan echt staat. Live capturen = de reflectie volgt de echte zon/maan-stand
					// EN 's nachts is de gevangen lucht donker (geen valse dag-reflectie meer).
					SLC->SetMobility(EComponentMobility::Movable);
					// GEEN continue real-time capture: die her-vangt/re-convolveert de skylight elke paar frames
					// (time-sliced) -> zachte AMBIENT-flicker/rand-puls, ook stilstaand (speler-melding 07-06).
					// I.p.v. dat: real-time UIT + hieronder een gecontroleerde recapture op een trage timer, zodat de
					// reflectie de zon/maan blijft volgen zonder de flicker.
					SLC->SetRealTimeCaptureEnabled(false);
					// Onderste hemisfeer NIET zwart: anders verlicht de skylight alleen omhoog-kijkende
					// vlakken (de vloer) en blijft de PLAFOND-onderkant pikzwart overdag. Nu krijgt de
					// plafond-onderzijde ook ambient -> geen zwart dak meer binnen bij daglicht.
					SLC->bLowerHemisphereIsBlack = false;
					SLC->RecaptureSky();
					FSkyDim SkEntry; SkEntry.Sky = SLC; SkEntry.OrigIntensity = SLC->Intensity;
					SLC->SetIntensity(SkEntry.OrigIntensity * FMath::Lerp(GNightSkyFloor, 1.f, MinDayF)); // meteen op klok-stand
					SkyDims.Add(SkEntry);
				}
				TInlineComponentArray<UStaticMeshComponent*> Meshes(A);
				for (UStaticMeshComponent* MC : Meshes)
				{
					if (!MC || !MC->GetStaticMesh()) { continue; }
					const FString MN = MC->GetStaticMesh()->GetName();
					// Sky-dome/HDRI-koepel met ingebakken (dag)zon: sky-specifieke namen die GEEN gebouwen/
					// water/skyscrapers raken. 's Nachts verbergen we 'm zodat de ingebakken zon niet toont.
					const FString AN = A->GetName();
					auto SkyLike = [](const FString& S)
					{
						return S.Contains(TEXT("EnviroDome")) || S.Contains(TEXT("HDRI")) || S.Contains(TEXT("Dome"))
							|| S.Contains(TEXT("Cyclorama")) || S.Contains(TEXT("Backdrop")) || S.Contains(TEXT("Horizon"))
							|| S.Contains(TEXT("SkySphere")) || S.Contains(TEXT("SkyDome")) || S.Contains(TEXT("SkyBox"))
							|| S.Contains(TEXT("Skybox")) || S.Contains(TEXT("EnviroSky"));
					};
					if (SkyLike(MN) || SkyLike(AN))
					{
						DomeComps.AddUnique(MC);
					}
					// PACK-LAMPEN: lantaarns/plafondlampen van de map zijn alleen mesh (geen licht).
					// Warm puntlicht eraan hangen; de klok zet ze 's avonds aan en 's ochtends uit.
					const bool bTallLamp = MN == TEXT("SM_StreetLight_01") || MN == TEXT("SM_StreetLight_02")
						|| MN == TEXT("SM_LampPostBeach_01") || MN == TEXT("SM_AlleyLamp") || MN == TEXT("SM_UtilityPole_Lamp");
					const bool bCeilLamp = MN.Contains(TEXT("CeilingLight")) || MN.Contains(TEXT("CeilLight")) || MN.Contains(TEXT("Ceiling_Light"));
					const bool bSmallLamp = bCeilLamp
						|| MN == TEXT("SM_WallLight01") || MN == TEXT("SM_PoolLight");
					// PROP-CULL (Draw-winst richting de stad): kleine losse props (stoelen, tafels, prullenbakken, borden,
						// terras-decor) op afstand uit beeld halen. Richting de stad staan er honderden in beeld = honderden
						// draw calls (Draw ~10ms naar zee -> ~22ms naar de stad); een stoel op ~70m is een paar pixels. Cull-
						// afstand op alleen de KLEINE Static-meshes (bounds-radius < 1.8m) -> gebouwen (Nanite), weg, auto's en
						// grote props blijven. r.ViewDistanceScale (per tier) schaalt mee: potato ~70m, epic ~260m = tier-trap.
						if (!bTallLamp && !bSmallLamp && !SkyLike(MN) && !SkyLike(AN)
							&& MC->Mobility == EComponentMobility::Static && MC->Bounds.SphereRadius < 320.f)
						{
							// Drempel 180->320cm: tafels/parasols/plantenbakken (terras-meubilair) vielen er net buiten
							// terwijl juist DIE met honderden in de cafe-zones staan (gemeten: 4942 draws / Draw 23ms op de
							// strip vs ~2300 / 10ms elders). Gebouwen (Nanite), weg en auto's blijven ruim boven de drempel.
							MC->SetCullDistance(20000.f); // ~70m op potato (x r.ViewDistanceScale 0.35); engine dither-fade = pop-arm
						}
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
						else if (bCeilLamp)
						{
							// Plafondlamp: SPOT recht omlaag met een WIJDE, zachte kegel -> nette lichtkegel
							// op de vloer + room-glow, i.p.v. alleen een klein puntje bij de lamp.
							USpotLightComponent* CSp = NewObject<USpotLightComponent>(this);
							CSp->SetupAttachment(GetRootComponent());
							CSp->RegisterComponent();
							CSp->SetWorldLocationAndRotation(BO + FVector(0.f, 0.f, -8.f), FRotator(-90.f, 0.f, 0.f));
							CSp->SetInnerConeAngle(26.f);   // helder hart
							CSp->SetOuterConeAngle(80.f);   // wijd + zachte rand
							CSp->SetAttenuationRadius(1400.f);
							PL = CSp;
							// EXTRA: een zacht OMNI-puntje bij dezelfde lamp. De neerwaartse spot laat het
							// plafond + de hoeken donker; dit puntje vult de kamer (incl. plafond) met een
							// warme gloed. Samen = nette kegel op de vloer EN room-glow rondom.
							UPointLightComponent* Glow = NewObject<UPointLightComponent>(this);
							Glow->SetupAttachment(GetRootComponent());
							Glow->RegisterComponent();
							Glow->SetWorldLocation(BO);
							Glow->SetAttenuationRadius(750.f);
							Glow->SetMobility(EComponentMobility::Movable);
							Glow->bUseInverseSquaredFalloff = false;
							Glow->LightFalloffExponent = 1.3f;
							Glow->SetLightColor(FLinearColor(1.f, 0.85f, 0.6f));
							Glow->SetIntensity(0.f);
							Glow->SetCastShadows(false);
							Glow->ComponentTags.Add(TEXT("CeilGlow"));
							Glow->SetMaxDrawDistance(25000.f); Glow->SetMaxDistanceFadeRange(3000.f); // mee-cullen met de view distance
							Glow->MarkRenderStateDirty();
							PackLampLights.Add(Glow);
							// Diffuser-mesh (MI_Light, 'Brightness') mee dimmen als de lamp uit is -> de lamp-box
							// gloeit niet meer wit zonder licht te geven.
							for (int32 mi = 0; mi < MC->GetNumMaterials(); ++mi)
							{
								UMaterialInterface* Mat = MC->GetMaterial(mi);
								if (Mat && (Mat->GetName().Contains(TEXT("Light")) || Mat->GetName().Contains(TEXT("Emmis")) || Mat->GetName().Contains(TEXT("Emis"))))
								{
									if (UMaterialInstanceDynamic* EMID = MC->CreateDynamicMaterialInstance(mi))
									{
										float Orig = 1.f; EMID->GetScalarParameterValue(TEXT("Brightness"), Orig);
										PackCeilEmis.Add(EMID);
										PackCeilEmisBright.Add(Orig > 0.f ? Orig : 1.f);
										PackCeilEmisPos.Add(BO);
									}
								}
							}
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
						PL->LightFalloffExponent = bCeilLamp ? 1.6f : 2.5f; // plafond: zachtere falloff = meer room-fill
						PL->SetLightColor(FLinearColor(1.f, 0.85f, 0.6f));
						PL->SetIntensity(0.f);
						PL->SetCastShadows(false);
						PL->SetMaxDrawDistance(25000.f); PL->SetMaxDistanceFadeRange(3000.f); // mee-cullen met de view distance
						PL->MarkRenderStateDirty();
						PL->ComponentTags.Add(bTallLamp ? TEXT("TallLamp") : (bCeilLamp ? TEXT("CeilLamp") : TEXT("SmallLamp")));
						PackLampLights.Add(PL);
					}
				}
			}
			// BACK-OFF: vond deze scan niks nieuws, dan is de map gestreamd/stabiel -> scan-interval naar 30s
			// (geen periodieke 6s-hitch meer tijdens normaal spelen). Weer snel (6s) zodra er iets opduikt.
			const int32 SeenAfter = SeenLights.Num() + SeenRefCaps.Num() + SeenSky.Num() + PackLampSeen.Num();
			if (SeenAfter > SeenBefore) { LightScanDry = 0; LightScanTimer = 6.f; }
			else if (++LightScanDry >= 3) { LightScanTimer = 30.f; }
		}

		// LICHT-BUDGET-POOL (foundationele perf-fix): verberg de VERRE Movable-lampen (distance-cull, vastgeknoopt aan de render-fade) -> uit de count + Lighting.
		// De controller + pack registreren ~1186 echte Movable-lampen (817 eigen PackLampLights + 569 Movable map-lights);
		// InitViews/Lighting schalen met dat AANTAL (de afstand-cull dimt alleen het RENDEREN, niet de count). SetVisibility(false)
		// op de verre lampen haalt ze uit FScene::Lights = uit de count. Periodiek (0.35s), niet per frame; GetVisibleFlag-check
		// vermijdt onnodige SetVisibility-churn. Zo schaalt de licht-kost met de POOL, niet met hoeveel content er nog bijkomt.
		LightBudgetTimer -= DeltaSeconds;
		if (LightBudgetTimer <= 0.f)
		{
			LightBudgetTimer = 0.35f;
			FVector PP(0.f);
			if (APlayerController* PC = GetWorld()->GetFirstPlayerController()) { if (APawn* Pn = PC->GetPawn()) { PP = Pn->GetActorLocation(); } }
				static IConsoleVariable* CVarLMD = IConsoleManager::Get().FindConsoleVariable(TEXT("r.LightMaxDrawDistanceScale"));
				const float Scale = CVarLMD ? FMath::Max(CVarLMD->GetFloat(), 0.05f) : 1.f;
				// Hide-afstand is PER LAMP (LampHideD2 in ApplyBudget): render-cull-afstand + marge; de lamp is daar
				// via de renderer al uitgefade -> SetVisibility = pop-vrij. Decor-lichtjes krijgen een kortere afstand.
				// Max flips per pass: bij de dag/nacht-drempel slaan honderden lampen TEGELIJK om; al die
				// SetVisibility's in EEN frame = primitive add/remove-burst -> InitViews-stall op de render-thread
				// (gemeten 65-77ms hitches bij dageraad). De rest schuift door naar de volgende pass (0.35s later) -
				// visueel onzichtbaar: de lampen staan op intensiteit 0 of voorbij de render-fade.
				int32 FlipsLeft = 48;
				auto ApplyBudget = [&](ULightComponent* L)
				{
					if (!L || FlipsLeft <= 0) { return; }
					// GELAAGD budget: DECOR-lichtjes (klein bereik: palm-slingers, gevel-spots, raam-gloed) faden +
					// verdwijnen op ~2,3x kortere afstand dan straatlampen. Een 3-6m-lampje draagt op 40m niets meer
					// bij maar kost wel een volle light-pass; gemeten in de neon-zones 's nachts: 662 lights in scene
					// -> Lighting 8,5ms van de 22ms Draw. MaxDrawDistance+FadeRange 1x per lamp zetten -> de
					// renderer-fade schaalt mee (fade-einde VOOR onze hide-afstand) -> pop-vrij.
					float LampMaxD = 25000.f;
					if (const UPointLightComponent* PLc = Cast<UPointLightComponent>(L))
					{
						if (PLc->AttenuationRadius < 1500.f) // gemeten (PDIAG): 0 lampen <6.5m, de bulk (953) zit in 6.5-15m -> die klasse IS het decor
						{
							LampMaxD = 11000.f;
							if (L->MaxDrawDistance != 11000.f)
							{
								L->SetMaxDrawDistance(11000.f);
								L->SetMaxDistanceFadeRange(3000.f);
							}
						}
					}
					const float LampHideD2 = FMath::Square(LampMaxD * Scale + 2000.f);
					const bool bVis = (L->Intensity > 1.f) && (FVector::DistSquared(L->GetComponentLocation(), PP) < LampHideD2); // ook UIT-lampen (intensiteit ~0, bv. straatlampen overdag) verbergen: die kostten anders volle Lighting voor niks. Pop-vrij want ze staan toch al op 0.
					if (L->GetVisibleFlag() != bVis) { L->SetVisibility(bVis); --FlipsLeft; }
				};
				for (const FDimLight& D : DimLights) { ApplyBudget(D.Light.Get()); }
				for (UPointLightComponent* PL : PackLampLights) { ApplyBudget(PL); }
		}

		// Per-frame licht-loops alleen draaien als de klok-factor of de lamp-slider echt veranderde (+ 2Hz
		// vangnet voor een externe resetter). Anders zijn de gewenste intensiteiten identiek aan vorige frame
		// -> de hele iteratie over honderden lichten overslaan = pure CPU-winst, geen zichtbaar verschil.
		LightUpdateTimer -= DeltaSeconds;
		const bool bUpdateLights = FMath::Abs(MinDayF - LastLightUpdateMinDayF) > 0.003f
			|| !FMath::IsNearlyEqual(LampIntensity, LastLightUpdateLampI, 1.f)
			|| LightUpdateTimer <= 0.f;
		if (bUpdateLights) { LastLightUpdateMinDayF = MinDayF; LastLightUpdateLampI = LampIntensity; LightUpdateTimer = 0.5f; }

		// Lichten dimmen. ZONLICHT (directional) moet 's nachts ECHT uit - een restje van 7% werd
		// door de auto-exposure weer opgeblazen tot daglicht onder een zwarte hemel. Skylight bijna
		// uit; alle overige lichten 7% (de emissive strips van de map blijven vanzelf - materiaal).
		const float SunMul = 0.f;                           // map-zonnen permanent uit: onze bewegende zon is DE zon
		const float SkyMul = FMath::Lerp(GNightSkyFloor, 1.f, MinDayF); // D.5: nacht-vloer omhoog (was 0.02) - hemel nooit pikkedonker
		if (bUpdateLights)
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
		// REFLECTION CAPTURES mee-dimmen met de klok: de map heeft sphere/box reflection captures die
		// overdag zijn gebakken en de (al gedimde) skylight-specular BINNEN hun straal overschrijven ->
		// ramen/water bleven 's nachts daglicht spiegelen. Brightness * dag-factor (0 = diepe nacht)
		// haalt die dag-reflectie weg; SSR + neon vullen het nacht-beeld. De dag-look blijft 100% intact.
		if (!FMath::IsNearlyEqual(LastRefMul, MinDayF, 0.02f))
		{
			LastRefMul = MinDayF;
			for (FRefCap& R : RefCaps)
			{
				if (UReflectionCaptureComponent* RC = R.Cap.Get())
				{
					RC->Brightness = R.OrigBrightness * MinDayF;
					RC->MarkRenderStateDirty();
				}
			}
		}
		// SKYLIGHT-SPECULAR mee-dimmen (ELKE tick, met diff-check): dit is de ECHTE bron van de
		// nacht-reflectie op water/ramen - een movable skylight op volle sterkte spiegelt z'n
		// hemel-cubemap. Ongated zodat een eventuele resetter naar dag-sterkte meteen weer gedimd wordt.
		if (bUpdateLights)
		{
			const float SkyWant = FMath::Lerp(GNightSkyFloor, 1.f, MinDayF); // D.5: nacht-specular-vloer omhoog (was 0.06)
			for (FSkyDim& Sk : SkyDims)
			{
				if (USkyLightComponent* SLC = Sk.Sky.Get())
				{
					const float Want = Sk.OrigIntensity * SkyWant;
					if (!FMath::IsNearlyEqual(SLC->Intensity, Want, Sk.OrigIntensity * 0.01f + 0.001f)) { SLC->SetIntensity(Want); }
				}
			}
		}

		// Gecontroleerde skylight-recapture (i.p.v. continue real-time capture): elke ~2,5s de hemel opnieuw vangen
		// zodat de reflectie de zon/maan blijft volgen, MAAR zonder de per-frame re-convolutie die de zachte
		// ambient-flicker / rand-puls gaf (ook stilstaand). Elke capture is op zichzelf stabiel -> geen shimmer.
		SkyCaptureTimer -= DeltaSeconds;
		if (SkyCaptureTimer <= 0.f)
		{
			SkyCaptureTimer = 2.5f;
			for (FSkyDim& Sk : SkyDims)
			{
				if (USkyLightComponent* SLC = Sk.Sky.Get()) { SLC->RecaptureSky(); }
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
				// SSR UIT 's nachts: het oceaanwater (130 MI_Ocean-planes) en de ramen spiegelden via
				// screen-space reflections de resterende heldere horizon-rand -> die felle "zonsondergang"
				// op het water/de ramen midden in de nacht. Met dome verborgen, captures op 0 en skylight
				// bijna uit is SSR de laatste bron; die hier wegblenden haalt de nacht-reflectie definitief
				// weg. Schaalt mee met BlendWeight (1-MinDayF) dus overdag 100% intact.
				NightVol->Settings.bOverride_ScreenSpaceReflectionIntensity = true;
				NightVol->Settings.ScreenSpaceReflectionIntensity = 0.f;
				// Exposure-plafond (1.0) OOK bij nacht, dezelfde cap als de BloomPPV overdag. Stond de cap alleen op
				// BloomPPV (gewicht = MinDayF), dan viel hij weg tijdens de dag/nacht-overgang en kon de auto-exposure
				// kort naar wit overschieten (de "witte flits"). Cap in BEIDE volumes = over de hele blend ~1.0 = geen overshoot.
				NightVol->Settings.bOverride_AutoExposureMaxBrightness = true;
				NightVol->Settings.AutoExposureMaxBrightness = 1.0f;
				// D.5 exposure-BODEM (spiegel van de MaxBrightness-cap): 's nachts mag de auto-exposure niet
				// verder inzakken dan deze vloer -> voorkomt het pikkedonker rond 01:00. Onderkant begrensd,
				// bovenkant blijft op 1.0; de Night exposure/glow-sliders sturen nog steeds het niveau ertussen.
				NightVol->Settings.bOverride_AutoExposureMinBrightness = true;
				NightVol->Settings.AutoExposureMinBrightness = GNightExposureMinBrightness;
				NightPPV = NightVol;
			}
		}
		// HEMEL-WAAS temmen: de Mie-nevel van de map-atmosfeer maakte een gigantisch wazig
		// zonsgebied (halve hemel wit). Minder nevel-dichtheid + halo strakker om de schijf.
		if (!bUseUDS && !FMath::IsNearlyEqual(LastAppliedHaze, SunHaze, 0.00002f))
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
		// Per-lamp drempel hieronder: binnen-plafondlampen blijven bijna altijd aan, buitenlampen volgen de schemering.
		if (bUpdateLights)
		for (UPointLightComponent* PL : PackLampLights)
		{
			if (!PL) { continue; }
			// Door een lichtschakelaar overgenomen plafondlampen: de schakelaar bepaalt aan/uit + dim,
			// niet de klok -> hier overslaan.
			if (SwitchControlledLights.Contains(PL)) { continue; }
			const float Div = PL->ComponentHasTag(TEXT("TallLamp")) ? 6000.f
				: PL->ComponentHasTag(TEXT("CeilLamp")) ? 900.f
					: PL->ComponentHasTag(TEXT("CeilGlow")) ? 2500.f : 9000.f; // CeilLamp 900 = origineel (hoofdlamp dim -> vloer/ramen niet blown); CeilGlow 9000->2500 = zachte plafond-glow op zodat het PLAFOND verlicht; straat ongewijzigd; TUNEBAAR
			const bool bCeil = PL->ComponentHasTag(TEXT("CeilLamp")) || PL->ComponentHasTag(TEXT("CeilGlow"));
			// Plafondlampen (gebouw-/hotelgangen, interieurs) hebben geen daglicht -> ALTIJD aan.
			// Buitenlampen (straatlantaarns) volgen wel de schemering. Door een schakelaar geclaimde
			// lampen zijn hierboven al overgeslagen.
			const float Want = ((bCeil ? true : (MinDayF < 0.7f)) ? (LampIntensity / Div) : 0.f);
			if (!FMath::IsNearlyEqual(PL->Intensity, Want, 0.05f)) { PL->SetIntensity(Want); }
		}

		// Diffuser-emissive ('Brightness'): plafond-boxen altijd mee-glow (lampen zijn altijd aan); door een
		// schakelaar geclaimde boxen worden hieronder overgeslagen.
		const float CeilEmisOn = 1.f;
		if (bUpdateLights)
		for (int32 i = 0; i < PackCeilEmis.Num(); ++i)
		{
			if (UMaterialInstanceDynamic* E = PackCeilEmis[i])
			{
				if (SwitchControlledEmis.Contains(E)) { continue; } // schakelaar bestuurt deze box-gloed
				const float B = PackCeilEmisBright.IsValidIndex(i) ? PackCeilEmisBright[i] : 1.f;
				E->SetScalarParameterValue(TEXT("Brightness"), B * CeilEmisOn);
			}
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
		if (!bUseUDS && !PackMoon.IsValid())
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
		if (!bUseUDS && !PackSun.IsValid())
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
		if (!bUseUDS && PackSun.IsValid() && PackSun->GetLightComponent())
		{
			// HOOGTE-MODEL: de altitude volgt de tijd - 0 op de horizon bij op-/ondergang, hoog 's
			// middags, en NEGATIEF (echt ONDER de horizon) 's nachts -> de zon zinkt realistisch weg.
			// De yaw zwaait uit en komt terug zodat op- EN ondergang bij NW-N (337.5) liggen, met de
			// reflectie die er tussenin meebeweegt. Intensiteit volgt de hoogte (uit onder de horizon),
			// dus de zon en maan zijn nooit tegelijk boven de horizon -> nooit 2 schijven.
			const float p = (Hour - Sunrise) / DayLen2;                 // NIET geklampt: <0/>1 = onder de horizon
			const float Alt = FMath::Sin(p * PI);                       // >0 boven horizon (dag), <0 onder (nacht)
			const float SunYaw = 337.5f + FMath::Sin(FMath::Clamp(p, 0.f, 1.f) * PI) * 50.f;
			PackSun->SetActorRotation(FRotator(-Alt * 78.f, SunYaw, 0.f)); // -78 hoog, +78 diep onder, 0 op horizon
			PackSun->GetLightComponent()->SetIntensity(SunIntensity * FMath::Clamp(Alt * 3.f, 0.f, 1.f));
		}
		// MAAN: tegenboog door de nacht - op bij zonsondergang, onder bij zonsopkomst, zelfde N-lijn.
		if (!bUseUDS && PackMoon.IsValid() && PackMoon->GetLightComponent())
		{
			// Zelfde hoogte-model voor de maan, maar op de NACHT-fase: boven de horizon 's nachts,
			// ONDER de horizon overdag (dus geen maan-schijf bij daglicht). Op/onder ook bij NW-N.
			const float NightLen = FMath::Max(1.f, 24.f - DayLen2);
			const float np = (Hour >= Sunset) ? (Hour - Sunset) / NightLen : (Hour + 24.f - Sunset) / NightLen;
			const float MAlt = FMath::Sin(np * PI);                  // >0 boven horizon (nacht), <0 onder (dag)
			const float MnYaw = 337.5f + FMath::Sin(FMath::Clamp(np, 0.f, 1.f) * PI) * 50.f;
			PackMoon->SetActorRotation(FRotator(-MAlt * 78.f, MnYaw, 0.f));
			PackMoon->GetLightComponent()->SetIntensity(MoonIntensity * FMath::Clamp(MAlt * 3.f, 0.f, 1.f));
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
	if (!FFileHelper::LoadFileToStringArray(Lines, *WeedData::File(TEXT("LightConfig.txt")))) { return; }
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
		else if (K == TEXT("UdsExpDay"))      { UdsExpDay = F; }
		else if (K == TEXT("UdsExpDawnDusk")) { UdsExpDawnDusk = F; }
		else if (K == TEXT("UdsExpNight"))    { UdsExpNight = F; }
		else if (K == TEXT("UdsCloud"))       { UdsCloud = F; }
		else if (K == TEXT("UdsFog"))         { UdsFog = F; }
	}
	UE_LOG(LogTemp, Log, TEXT("[LightConfig] geladen (%d regels)"), Lines.Num());
}
